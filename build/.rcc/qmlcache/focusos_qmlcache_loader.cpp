#include <QtQml/qqmlprivate.h>
#include <QtCore/qdir.h>
#include <QtCore/qurl.h>
#include <QtCore/qhash.h>
#include <QtCore/qstring.h>

namespace QmlCacheGeneratedCode {
namespace _qt_qml_FocusOS_src_shell_Main_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_FocusOS_src_shell_ActivitiesPanel_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_FocusOS_src_shell_InfoPanel_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_FocusOS_src_shell_AmbientLayer_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_FocusOS_src_shell_MissionView_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_FocusOS_src_shell_NotesDrawer_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_FocusOS_src_shell_RoutineEditorDialog_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_FocusOS_src_shell_UnlockModal_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}

}
namespace {
struct Registry {
    Registry();
    ~Registry();
    QHash<QString, const QQmlPrivate::CachedQmlUnit*> resourcePathToCachedUnit;
    static const QQmlPrivate::CachedQmlUnit *lookupCachedUnit(const QUrl &url);
};

Q_GLOBAL_STATIC(Registry, unitRegistry)


Registry::Registry() {
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/Main.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_Main_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/ActivitiesPanel.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_ActivitiesPanel_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/InfoPanel.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_InfoPanel_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/AmbientLayer.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_AmbientLayer_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/MissionView.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_MissionView_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/NotesDrawer.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_NotesDrawer_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/RoutineEditorDialog.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_RoutineEditorDialog_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/FocusOS/src/shell/UnlockModal.qml"), &QmlCacheGeneratedCode::_qt_qml_FocusOS_src_shell_UnlockModal_qml::unit);
    QQmlPrivate::RegisterQmlUnitCacheHook registration;
    registration.structVersion = 0;
    registration.lookupCachedQmlUnit = &lookupCachedUnit;
    QQmlPrivate::qmlregister(QQmlPrivate::QmlUnitCacheHookRegistration, &registration);
}

Registry::~Registry() {
    QQmlPrivate::qmlunregister(QQmlPrivate::QmlUnitCacheHookRegistration, quintptr(&lookupCachedUnit));
}

const QQmlPrivate::CachedQmlUnit *Registry::lookupCachedUnit(const QUrl &url) {
    if (url.scheme() != QLatin1String("qrc"))
        return nullptr;
    QString resourcePath = QDir::cleanPath(url.path());
    if (resourcePath.isEmpty())
        return nullptr;
    if (!resourcePath.startsWith(QLatin1Char('/')))
        resourcePath.prepend(QLatin1Char('/'));
    return unitRegistry()->resourcePathToCachedUnit.value(resourcePath, nullptr);
}
}
int QT_MANGLE_NAMESPACE(qInitResources_qmlcache_focusos)() {
    ::unitRegistry();
    return 1;
}
Q_CONSTRUCTOR_FUNCTION(QT_MANGLE_NAMESPACE(qInitResources_qmlcache_focusos))
int QT_MANGLE_NAMESPACE(qCleanupResources_qmlcache_focusos)() {
    return 1;
}
