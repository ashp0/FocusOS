#include "core/MediaKeys.h"

#include "core/SystemStatus.h"
#include "platform/PlatformBackend.h"

#include <QLoggingCategory>
#include <QTimer>

#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#endif

#if defined(FOCUSOS_HAS_KGLOBALACCEL)
#include <KGlobalAccel>
#include <QAction>
#include <QKeySequence>
#endif

Q_LOGGING_CATEGORY(lcMediaKeys, "focusos.mediakeys")

namespace {
// Matches the per-key step used by the in-shell handler in Main.qml so the
// physical keys and the on-screen sliders move in the same increments. Only
// referenced from the KGlobalAccel callbacks, which are compiled out on builds
// without it (e.g. macOS) — hence maybe_unused to stay warning-clean there.
[[maybe_unused]] constexpr int kVolumeStep = 5;
[[maybe_unused]] constexpr int kBrightnessStep = 5;
} // namespace

MediaKeys::MediaKeys(SystemStatus *systemStatus, PlatformBackend *backend, QObject *parent)
    : QObject(parent)
    , m_systemStatus(systemStatus)
    , m_backend(backend)
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

#if defined(Q_OS_LINUX)
    // Watch logind for suspend/resume. On the resume edge the compositor may have
    // dropped our global-key grabs, so we re-claim them (see onPrepareForSleep).
    // This is the system bus signal the deep-idle suspend (and lid/power suspend)
    // both emit, so one subscription covers every way the machine can sleep.
    QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("PrepareForSleep"),
        this, SLOT(onPrepareForSleep(bool)));
#endif
}

bool MediaKeys::active() const
{
    return m_active;
}

void MediaKeys::onPrepareForSleep(bool aboutToSleep)
{
    if (aboutToSleep) {
        return; // Going down — nothing to do until we come back up.
    }
    // Just resumed. kglobalacceld may have been restarted and KWin can have
    // dropped the grabs, so re-establish everything. A short delay gives the
    // compositor / daemon a moment to settle after wake before we re-grab.
    QTimer::singleShot(1500, this, &MediaKeys::reclaimShortcuts);
}

void MediaKeys::reclaimShortcuts()
{
#if defined(FOCUSOS_HAS_KGLOBALACCEL)
    if (!m_active) {
        return;
    }
    // Make sure the global-shortcuts daemon is back up (a bare kwin_wayland
    // session has nothing else that would relaunch it after a resume), then
    // re-apply each binding so KWin re-grabs the keys.
    if (m_backend) {
        m_backend->ensureGlobalShortcutsDaemon();
    }
    for (const Binding &binding : m_bindings) {
        // Drop the existing registration first so setShortcut can't short-circuit
        // on an "unchanged" binding — we need KWin to re-issue the key grab, which
        // it only does on a fresh register.
        KGlobalAccel::self()->removeAllShortcuts(binding.action);
        applyShortcut(binding);
    }
    qCDebug(lcMediaKeys, "re-claimed %lld global media-key grabs after resume",
            static_cast<long long>(m_bindings.size()));
#endif
}

#if defined(FOCUSOS_HAS_KGLOBALACCEL)
QAction *MediaKeys::registerKey(const QString &id, const QString &displayName, int qtKey)
{
    auto *action = new QAction(displayName, this);
    // A stable objectName is the key KGlobalAccel persists the binding under.
    action->setObjectName(id);
    action->setProperty("componentName", QStringLiteral("focusos"));
    action->setProperty("componentDisplayName", QStringLiteral("FocusOS"));

    const Binding binding{action, qtKey};
    applyShortcut(binding);
    m_bindings.append(binding);
    return action;
}

void MediaKeys::applyShortcut(const Binding &binding)
{
    // NoAutoloading forces our default binding instead of reading whatever the
    // user's stored kglobalshortcutsrc has for this action — important because a
    // previous Plasma session may have left these media keys assigned to
    // plasma-pa/powerdevil, which aren't running here. Re-calling setShortcut is
    // also how reclaimShortcuts() re-grabs the keys after a resume.
    const bool ok = KGlobalAccel::self()->setShortcut(
        binding.action, {QKeySequence(binding.qtKey)}, KGlobalAccel::NoAutoloading);
    if (!ok) {
        qCWarning(lcMediaKeys, "failed to register global shortcut for %s",
                  qPrintable(binding.action->objectName()));
    }
}
#endif
