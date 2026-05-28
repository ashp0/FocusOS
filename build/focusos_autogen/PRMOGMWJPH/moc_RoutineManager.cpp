/****************************************************************************
** Meta object code from reading C++ file 'RoutineManager.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/RoutineManager.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'RoutineManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN14RoutineManagerE_t {};
} // unnamed namespace

template <> constexpr inline auto RoutineManager::qt_create_metaobjectdata<qt_meta_tag_ZN14RoutineManagerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "RoutineManager",
        "activeChanged",
        "",
        "remainingSecondsChanged",
        "accessChanged",
        "configChanged",
        "sessionPromptChanged",
        "pausedChanged",
        "editModeChanged",
        "desktopShellChanged",
        "routineCountChanged",
        "statusMessageChanged",
        "message",
        "networkLockPromptChanged",
        "desktopAccessRequested",
        "routineSessionFinished",
        "routineId",
        "routineName",
        "minutes",
        "result",
        "startedAt",
        "endedAt",
        "routineSessionProgress",
        "elapsedSeconds",
        "updatedAt",
        "engage",
        "startPendingRoutineWithoutNetworkLock",
        "abortPendingRoutineStart",
        "togglePause",
        "endActiveRoutine",
        "closeOtherAccess",
        "launchDesktopShell",
        "relaunchActiveRoutine",
        "unlockOtherAccess",
        "continueFinishedSession",
        "quitFinishedSession",
        "routinesForEditing",
        "QVariantList",
        "saveRoutines",
        "routines",
        "updateRoutineDescription",
        "description",
        "pickApplication",
        "applicationDisplayName",
        "path",
        "active",
        "activeRoutineId",
        "activeRoutineName",
        "activeRoutineTotalSeconds",
        "activeRoutineDescription",
        "remainingSeconds",
        "accessGranted",
        "accessRemainingSeconds",
        "accessStatus",
        "otherAccessMinutes",
        "sessionPromptVisible",
        "finishedSessionName",
        "finishedSessionMinutes",
        "finishedSessionResult",
        "paused",
        "editMode",
        "desktopShellSupported",
        "desktopShellRunning",
        "routineCount",
        "activeRoutineHasLaunchTargets",
        "statusMessage",
        "networkLockPromptVisible",
        "networkLockError",
        "networkLockRoutineName",
        "Role",
        "RoutineIdRole",
        "NameRole",
        "DescriptionRole",
        "AppsRole",
        "AppsDisplayRole",
        "AllowedUrlsRole",
        "TimeLimitMinutesRole",
        "MinTimeMinutesRole",
        "NetworkLockRole",
        "BreakFrequencyMinutesRole",
        "BreakDurationMinutesRole",
        "IsActiveRole",
        "TimeLabelRole",
        "ButtonLabelRole",
        "ButtonEnabledRole"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'activeChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'remainingSecondsChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'accessChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'configChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'sessionPromptChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'pausedChanged'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'editModeChanged'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'desktopShellChanged'
        QtMocHelpers::SignalData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'routineCountChanged'
        QtMocHelpers::SignalData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'statusMessageChanged'
        QtMocHelpers::SignalData<void(const QString &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 12 },
        }}),
        // Signal 'networkLockPromptChanged'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'desktopAccessRequested'
        QtMocHelpers::SignalData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'routineSessionFinished'
        QtMocHelpers::SignalData<void(const QString &, const QString &, int, const QString &, const QDateTime &, const QDateTime &)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 16 }, { QMetaType::QString, 17 }, { QMetaType::Int, 18 }, { QMetaType::QString, 19 },
            { QMetaType::QDateTime, 20 }, { QMetaType::QDateTime, 21 },
        }}),
        // Signal 'routineSessionProgress'
        QtMocHelpers::SignalData<void(const QString &, const QString &, int, const QDateTime &, const QDateTime &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 16 }, { QMetaType::QString, 17 }, { QMetaType::Int, 23 }, { QMetaType::QDateTime, 20 },
            { QMetaType::QDateTime, 24 },
        }}),
        // Method 'engage'
        QtMocHelpers::MethodData<void(const QString &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 16 },
        }}),
        // Method 'startPendingRoutineWithoutNetworkLock'
        QtMocHelpers::MethodData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'abortPendingRoutineStart'
        QtMocHelpers::MethodData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'togglePause'
        QtMocHelpers::MethodData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'endActiveRoutine'
        QtMocHelpers::MethodData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'closeOtherAccess'
        QtMocHelpers::MethodData<void()>(30, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'launchDesktopShell'
        QtMocHelpers::MethodData<void()>(31, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'relaunchActiveRoutine'
        QtMocHelpers::MethodData<void()>(32, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'unlockOtherAccess'
        QtMocHelpers::MethodData<void()>(33, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'continueFinishedSession'
        QtMocHelpers::MethodData<void()>(34, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'quitFinishedSession'
        QtMocHelpers::MethodData<void()>(35, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'routinesForEditing'
        QtMocHelpers::MethodData<QVariantList() const>(36, 2, QMC::AccessPublic, 0x80000000 | 37),
        // Method 'saveRoutines'
        QtMocHelpers::MethodData<bool(const QVariantList &)>(38, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 37, 39 },
        }}),
        // Method 'updateRoutineDescription'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(40, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 16 }, { QMetaType::QString, 41 },
        }}),
        // Method 'pickApplication'
        QtMocHelpers::MethodData<QString() const>(42, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'applicationDisplayName'
        QtMocHelpers::MethodData<QString(const QString &) const>(43, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 44 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'active'
        QtMocHelpers::PropertyData<bool>(45, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineId'
        QtMocHelpers::PropertyData<QString>(46, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineName'
        QtMocHelpers::PropertyData<QString>(47, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineTotalSeconds'
        QtMocHelpers::PropertyData<int>(48, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineDescription'
        QtMocHelpers::PropertyData<QString>(49, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'remainingSeconds'
        QtMocHelpers::PropertyData<int>(50, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'elapsedSeconds'
        QtMocHelpers::PropertyData<int>(23, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'accessGranted'
        QtMocHelpers::PropertyData<bool>(51, QMetaType::Bool, QMC::DefaultPropertyFlags, 2),
        // property 'accessRemainingSeconds'
        QtMocHelpers::PropertyData<int>(52, QMetaType::Int, QMC::DefaultPropertyFlags, 2),
        // property 'accessStatus'
        QtMocHelpers::PropertyData<QString>(53, QMetaType::QString, QMC::DefaultPropertyFlags, 2),
        // property 'otherAccessMinutes'
        QtMocHelpers::PropertyData<int>(54, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'sessionPromptVisible'
        QtMocHelpers::PropertyData<bool>(55, QMetaType::Bool, QMC::DefaultPropertyFlags, 4),
        // property 'finishedSessionName'
        QtMocHelpers::PropertyData<QString>(56, QMetaType::QString, QMC::DefaultPropertyFlags, 4),
        // property 'finishedSessionMinutes'
        QtMocHelpers::PropertyData<int>(57, QMetaType::Int, QMC::DefaultPropertyFlags, 4),
        // property 'finishedSessionResult'
        QtMocHelpers::PropertyData<QString>(58, QMetaType::QString, QMC::DefaultPropertyFlags, 4),
        // property 'paused'
        QtMocHelpers::PropertyData<bool>(59, QMetaType::Bool, QMC::DefaultPropertyFlags, 5),
        // property 'editMode'
        QtMocHelpers::PropertyData<bool>(60, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 6),
        // property 'desktopShellSupported'
        QtMocHelpers::PropertyData<bool>(61, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'desktopShellRunning'
        QtMocHelpers::PropertyData<bool>(62, QMetaType::Bool, QMC::DefaultPropertyFlags, 7),
        // property 'routineCount'
        QtMocHelpers::PropertyData<int>(63, QMetaType::Int, QMC::DefaultPropertyFlags, 8),
        // property 'activeRoutineHasLaunchTargets'
        QtMocHelpers::PropertyData<bool>(64, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'statusMessage'
        QtMocHelpers::PropertyData<QString>(65, QMetaType::QString, QMC::DefaultPropertyFlags, 9),
        // property 'networkLockPromptVisible'
        QtMocHelpers::PropertyData<bool>(66, QMetaType::Bool, QMC::DefaultPropertyFlags, 10),
        // property 'networkLockError'
        QtMocHelpers::PropertyData<QString>(67, QMetaType::QString, QMC::DefaultPropertyFlags, 10),
        // property 'networkLockRoutineName'
        QtMocHelpers::PropertyData<QString>(68, QMetaType::QString, QMC::DefaultPropertyFlags, 10),
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'Role'
        QtMocHelpers::EnumData<enum Role>(69, 69, QMC::EnumFlags{}).add({
            {   70, Role::RoutineIdRole },
            {   71, Role::NameRole },
            {   72, Role::DescriptionRole },
            {   73, Role::AppsRole },
            {   74, Role::AppsDisplayRole },
            {   75, Role::AllowedUrlsRole },
            {   76, Role::TimeLimitMinutesRole },
            {   77, Role::MinTimeMinutesRole },
            {   78, Role::NetworkLockRole },
            {   79, Role::BreakFrequencyMinutesRole },
            {   80, Role::BreakDurationMinutesRole },
            {   81, Role::IsActiveRole },
            {   82, Role::TimeLabelRole },
            {   83, Role::ButtonLabelRole },
            {   84, Role::ButtonEnabledRole },
        }),
    };
    return QtMocHelpers::metaObjectData<RoutineManager, qt_meta_tag_ZN14RoutineManagerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject RoutineManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractListModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14RoutineManagerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14RoutineManagerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14RoutineManagerE_t>.metaTypes,
    nullptr
} };

void RoutineManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RoutineManager *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->activeChanged(); break;
        case 1: _t->remainingSecondsChanged(); break;
        case 2: _t->accessChanged(); break;
        case 3: _t->configChanged(); break;
        case 4: _t->sessionPromptChanged(); break;
        case 5: _t->pausedChanged(); break;
        case 6: _t->editModeChanged(); break;
        case 7: _t->desktopShellChanged(); break;
        case 8: _t->routineCountChanged(); break;
        case 9: _t->statusMessageChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->networkLockPromptChanged(); break;
        case 11: _t->desktopAccessRequested(); break;
        case 12: _t->routineSessionFinished((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[6]))); break;
        case 13: _t->routineSessionProgress((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[5]))); break;
        case 14: _t->engage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 15: _t->startPendingRoutineWithoutNetworkLock(); break;
        case 16: _t->abortPendingRoutineStart(); break;
        case 17: _t->togglePause(); break;
        case 18: _t->endActiveRoutine(); break;
        case 19: _t->closeOtherAccess(); break;
        case 20: _t->launchDesktopShell(); break;
        case 21: _t->relaunchActiveRoutine(); break;
        case 22: _t->unlockOtherAccess(); break;
        case 23: _t->continueFinishedSession(); break;
        case 24: _t->quitFinishedSession(); break;
        case 25: { QVariantList _r = _t->routinesForEditing();
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 26: { bool _r = _t->saveRoutines((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 27: { bool _r = _t->updateRoutineDescription((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 28: { QString _r = _t->pickApplication();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 29: { QString _r = _t->applicationDisplayName((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::activeChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::remainingSecondsChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::accessChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::configChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::sessionPromptChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::pausedChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::editModeChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::desktopShellChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::routineCountChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)(const QString & )>(_a, &RoutineManager::statusMessageChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::networkLockPromptChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)()>(_a, &RoutineManager::desktopAccessRequested, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)(const QString & , const QString & , int , const QString & , const QDateTime & , const QDateTime & )>(_a, &RoutineManager::routineSessionFinished, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)(const QString & , const QString & , int , const QDateTime & , const QDateTime & )>(_a, &RoutineManager::routineSessionProgress, 13))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->active(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->activeRoutineId(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->activeRoutineName(); break;
        case 3: *reinterpret_cast<int*>(_v) = _t->activeRoutineTotalSeconds(); break;
        case 4: *reinterpret_cast<QString*>(_v) = _t->activeRoutineDescription(); break;
        case 5: *reinterpret_cast<int*>(_v) = _t->remainingSeconds(); break;
        case 6: *reinterpret_cast<int*>(_v) = _t->elapsedSeconds(); break;
        case 7: *reinterpret_cast<bool*>(_v) = _t->accessGranted(); break;
        case 8: *reinterpret_cast<int*>(_v) = _t->accessRemainingSeconds(); break;
        case 9: *reinterpret_cast<QString*>(_v) = _t->accessStatus(); break;
        case 10: *reinterpret_cast<int*>(_v) = _t->otherAccessMinutes(); break;
        case 11: *reinterpret_cast<bool*>(_v) = _t->sessionPromptVisible(); break;
        case 12: *reinterpret_cast<QString*>(_v) = _t->finishedSessionName(); break;
        case 13: *reinterpret_cast<int*>(_v) = _t->finishedSessionMinutes(); break;
        case 14: *reinterpret_cast<QString*>(_v) = _t->finishedSessionResult(); break;
        case 15: *reinterpret_cast<bool*>(_v) = _t->paused(); break;
        case 16: *reinterpret_cast<bool*>(_v) = _t->editMode(); break;
        case 17: *reinterpret_cast<bool*>(_v) = _t->desktopShellSupported(); break;
        case 18: *reinterpret_cast<bool*>(_v) = _t->desktopShellRunning(); break;
        case 19: *reinterpret_cast<int*>(_v) = _t->routineCount(); break;
        case 20: *reinterpret_cast<bool*>(_v) = _t->activeRoutineHasLaunchTargets(); break;
        case 21: *reinterpret_cast<QString*>(_v) = _t->statusMessage(); break;
        case 22: *reinterpret_cast<bool*>(_v) = _t->networkLockPromptVisible(); break;
        case 23: *reinterpret_cast<QString*>(_v) = _t->networkLockError(); break;
        case 24: *reinterpret_cast<QString*>(_v) = _t->networkLockRoutineName(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 10: _t->setOtherAccessMinutes(*reinterpret_cast<int*>(_v)); break;
        case 16: _t->setEditMode(*reinterpret_cast<bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *RoutineManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RoutineManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14RoutineManagerE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(_clname);
}

int RoutineManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractListModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 30)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 30;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 30)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 30;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 25;
    }
    return _id;
}

// SIGNAL 0
void RoutineManager::activeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void RoutineManager::remainingSecondsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void RoutineManager::accessChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void RoutineManager::configChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void RoutineManager::sessionPromptChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void RoutineManager::pausedChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void RoutineManager::editModeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void RoutineManager::desktopShellChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void RoutineManager::routineCountChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void RoutineManager::statusMessageChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void RoutineManager::networkLockPromptChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void RoutineManager::desktopAccessRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void RoutineManager::routineSessionFinished(const QString & _t1, const QString & _t2, int _t3, const QString & _t4, const QDateTime & _t5, const QDateTime & _t6)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1, _t2, _t3, _t4, _t5, _t6);
}

// SIGNAL 13
void RoutineManager::routineSessionProgress(const QString & _t1, const QString & _t2, int _t3, const QDateTime & _t4, const QDateTime & _t5)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2, _t3, _t4, _t5);
}
QT_WARNING_POP
