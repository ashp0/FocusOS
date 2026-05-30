#include "core/MediaKeys.h"

#include "core/SystemStatus.h"

#include <QLoggingCategory>

#if defined(FOCUSOS_HAS_KGLOBALACCEL)
#include <KGlobalAccel>
#include <QAction>
#include <QKeySequence>
#endif

Q_LOGGING_CATEGORY(lcMediaKeys, "focusos.mediakeys")

namespace {
// Matches the per-key step used by the in-shell handler in Main.qml so the
// physical keys and the on-screen sliders move in the same increments.
constexpr int kVolumeStep = 5;
constexpr int kBrightnessStep = 5;
} // namespace

MediaKeys::MediaKeys(SystemStatus *systemStatus, QObject *parent)
    : QObject(parent)
    , m_systemStatus(systemStatus)
{
#if defined(FOCUSOS_HAS_KGLOBALACCEL)
    if (!m_systemStatus) {
        return;
    }

    QAction *volumeUp = registerKey(QStringLiteral("volume_up"),
                                    QStringLiteral("Increase Volume"), Qt::Key_VolumeUp);
    QAction *volumeDown = registerKey(QStringLiteral("volume_down"),
                                      QStringLiteral("Decrease Volume"), Qt::Key_VolumeDown);
    QAction *mute = registerKey(QStringLiteral("volume_mute"),
                                QStringLiteral("Mute"), Qt::Key_VolumeMute);
    QAction *brightnessUp = registerKey(QStringLiteral("brightness_up"),
                                        QStringLiteral("Increase Brightness"), Qt::Key_MonBrightnessUp);
    QAction *brightnessDown = registerKey(QStringLiteral("brightness_down"),
                                          QStringLiteral("Decrease Brightness"), Qt::Key_MonBrightnessDown);

    connect(volumeUp, &QAction::triggered, this, [this] {
        m_systemStatus->adjustSystemVolume(kVolumeStep);
    });
    connect(volumeDown, &QAction::triggered, this, [this] {
        m_systemStatus->adjustSystemVolume(-kVolumeStep);
    });
    connect(mute, &QAction::triggered, this, [this] {
        m_systemStatus->toggleMute();
    });
    connect(brightnessUp, &QAction::triggered, this, [this] {
        m_systemStatus->adjustBrightness(kBrightnessStep);
    });
    connect(brightnessDown, &QAction::triggered, this, [this] {
        m_systemStatus->adjustBrightness(-kBrightnessStep);
    });

    m_active = true;
    qCDebug(lcMediaKeys, "registered global volume + brightness shortcuts with the compositor");
#else
    qCDebug(lcMediaKeys, "built without KF6GlobalAccel — media keys only work while FocusOS is focused");
#endif
}

bool MediaKeys::active() const
{
    return m_active;
}

#if defined(FOCUSOS_HAS_KGLOBALACCEL)
QAction *MediaKeys::registerKey(const QString &id, const QString &displayName, int qtKey)
{
    auto *action = new QAction(displayName, this);
    // A stable objectName is the key KGlobalAccel persists the binding under.
    action->setObjectName(id);
    action->setProperty("componentName", QStringLiteral("focusos"));
    action->setProperty("componentDisplayName", QStringLiteral("FocusOS"));

    // NoAutoloading forces our default binding instead of reading whatever the
    // user's stored kglobalshortcutsrc has for this action — important because a
    // previous Plasma session may have left these media keys assigned to
    // plasma-pa/powerdevil, which aren't running here.
    const bool ok = KGlobalAccel::self()->setShortcut(
        action, {QKeySequence(qtKey)}, KGlobalAccel::NoAutoloading);
    if (!ok) {
        qCWarning(lcMediaKeys, "failed to register global shortcut for %s", qPrintable(id));
    }
    return action;
}
#endif
