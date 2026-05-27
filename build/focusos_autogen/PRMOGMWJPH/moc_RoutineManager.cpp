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
        "togglePause",
        "endActiveRoutine",
        "closeOtherAccess",
        "launchDesktopShell",
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
        "Role",
        "RoutineIdRole",
        "NameRole",
        "DescriptionRole",
        "AppsRole",
        "AppsDisplayRole",
        "AllowedUrlsRole",
        "TimeLimitMinutesRole",
        "MinTimeMinutesRole",
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
        // Signal 'routineSessionFinished'
        QtMocHelpers::SignalData<void(const QString &, const QString &, int, const QString &, const QDateTime &, const QDateTime &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 14 }, { QMetaType::QString, 15 }, { QMetaType::Int, 16 }, { QMetaType::QString, 17 },
            { QMetaType::QDateTime, 18 }, { QMetaType::QDateTime, 19 },
        }}),
        // Signal 'routineSessionProgress'
        QtMocHelpers::SignalData<void(const QString &, const QString &, int, const QDateTime &, const QDateTime &)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 14 }, { QMetaType::QString, 15 }, { QMetaType::Int, 21 }, { QMetaType::QDateTime, 18 },
            { QMetaType::QDateTime, 22 },
        }}),
        // Method 'engage'
        QtMocHelpers::MethodData<void(const QString &)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 14 },
        }}),
        // Method 'togglePause'
        QtMocHelpers::MethodData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'endActiveRoutine'
        QtMocHelpers::MethodData<void()>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'closeOtherAccess'
        QtMocHelpers::MethodData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'launchDesktopShell'
        QtMocHelpers::MethodData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'unlockOtherAccess'
        QtMocHelpers::MethodData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'continueFinishedSession'
        QtMocHelpers::MethodData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'quitFinishedSession'
        QtMocHelpers::MethodData<void()>(30, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'routinesForEditing'
        QtMocHelpers::MethodData<QVariantList() const>(31, 2, QMC::AccessPublic, 0x80000000 | 32),
        // Method 'saveRoutines'
        QtMocHelpers::MethodData<bool(const QVariantList &)>(33, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 32, 34 },
        }}),
        // Method 'updateRoutineDescription'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(35, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 14 }, { QMetaType::QString, 36 },
        }}),
        // Method 'pickApplication'
        QtMocHelpers::MethodData<QString() const>(37, 2, QMC::AccessPublic, QMetaType::QString),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'active'
        QtMocHelpers::PropertyData<bool>(38, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineId'
        QtMocHelpers::PropertyData<QString>(39, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineName'
        QtMocHelpers::PropertyData<QString>(40, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineTotalSeconds'
        QtMocHelpers::PropertyData<int>(41, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'activeRoutineDescription'
        QtMocHelpers::PropertyData<QString>(42, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'remainingSeconds'
        QtMocHelpers::PropertyData<int>(43, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'elapsedSeconds'
        QtMocHelpers::PropertyData<int>(21, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'accessGranted'
        QtMocHelpers::PropertyData<bool>(44, QMetaType::Bool, QMC::DefaultPropertyFlags, 2),
        // property 'accessRemainingSeconds'
        QtMocHelpers::PropertyData<int>(45, QMetaType::Int, QMC::DefaultPropertyFlags, 2),
        // property 'accessStatus'
        QtMocHelpers::PropertyData<QString>(46, QMetaType::QString, QMC::DefaultPropertyFlags, 2),
        // property 'otherAccessMinutes'
        QtMocHelpers::PropertyData<int>(47, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'sessionPromptVisible'
        QtMocHelpers::PropertyData<bool>(48, QMetaType::Bool, QMC::DefaultPropertyFlags, 4),
        // property 'finishedSessionName'
        QtMocHelpers::PropertyData<QString>(49, QMetaType::QString, QMC::DefaultPropertyFlags, 4),
        // property 'finishedSessionMinutes'
        QtMocHelpers::PropertyData<int>(50, QMetaType::Int, QMC::DefaultPropertyFlags, 4),
        // property 'finishedSessionResult'
        QtMocHelpers::PropertyData<QString>(51, QMetaType::QString, QMC::DefaultPropertyFlags, 4),
        // property 'paused'
        QtMocHelpers::PropertyData<bool>(52, QMetaType::Bool, QMC::DefaultPropertyFlags, 5),
        // property 'editMode'
        QtMocHelpers::PropertyData<bool>(53, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 6),
        // property 'desktopShellSupported'
        QtMocHelpers::PropertyData<bool>(54, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'desktopShellRunning'
        QtMocHelpers::PropertyData<bool>(55, QMetaType::Bool, QMC::DefaultPropertyFlags, 7),
        // property 'routineCount'
        QtMocHelpers::PropertyData<int>(56, QMetaType::Int, QMC::DefaultPropertyFlags, 8),
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'Role'
        QtMocHelpers::EnumData<enum Role>(57, 57, QMC::EnumFlags{}).add({
            {   58, Role::RoutineIdRole },
            {   59, Role::NameRole },
            {   60, Role::DescriptionRole },
            {   61, Role::AppsRole },
            {   62, Role::AppsDisplayRole },
            {   63, Role::AllowedUrlsRole },
            {   64, Role::TimeLimitMinutesRole },
            {   65, Role::MinTimeMinutesRole },
            {   66, Role::IsActiveRole },
            {   67, Role::TimeLabelRole },
            {   68, Role::ButtonLabelRole },
            {   69, Role::ButtonEnabledRole },
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
        case 10: _t->routineSessionFinished((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[6]))); break;
        case 11: _t->routineSessionProgress((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[5]))); break;
        case 12: _t->engage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->togglePause(); break;
        case 14: _t->endActiveRoutine(); break;
        case 15: _t->closeOtherAccess(); break;
        case 16: _t->launchDesktopShell(); break;
        case 17: _t->unlockOtherAccess(); break;
        case 18: _t->continueFinishedSession(); break;
        case 19: _t->quitFinishedSession(); break;
        case 20: { QVariantList _r = _t->routinesForEditing();
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 21: { bool _r = _t->saveRoutines((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 22: { bool _r = _t->updateRoutineDescription((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 23: { QString _r = _t->pickApplication();
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
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)(const QString & , const QString & , int , const QString & , const QDateTime & , const QDateTime & )>(_a, &RoutineManager::routineSessionFinished, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (RoutineManager::*)(const QString & , const QString & , int , const QDateTime & , const QDateTime & )>(_a, &RoutineManager::routineSessionProgress, 11))
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
        if (_id < 24)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 24;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 24)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 24;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
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
void RoutineManager::routineSessionFinished(const QString & _t1, const QString & _t2, int _t3, const QString & _t4, const QDateTime & _t5, const QDateTime & _t6)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1, _t2, _t3, _t4, _t5, _t6);
}

// SIGNAL 11
void RoutineManager::routineSessionProgress(const QString & _t1, const QString & _t2, int _t3, const QDateTime & _t4, const QDateTime & _t5)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2, _t3, _t4, _t5);
}
QT_WARNING_POP
