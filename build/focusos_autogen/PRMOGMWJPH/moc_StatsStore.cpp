/****************************************************************************
** Meta object code from reading C++ file 'StatsStore.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/StatsStore.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'StatsStore.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10StatsStoreE_t {};
} // unnamed namespace

template <> constexpr inline auto StatsStore::qt_create_metaobjectdata<qt_meta_tag_ZN10StatsStoreE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "StatsStore",
        "statsChanged",
        "",
        "targetChanged",
        "recordRoutineSession",
        "routineId",
        "routineName",
        "minutes",
        "result",
        "startedAt",
        "endedAt",
        "recordRoutineSessionProgress",
        "elapsedSeconds",
        "updatedAt",
        "recordLastSessionReflection",
        "reflection",
        "dailyStats",
        "QVariantList",
        "focusHistory",
        "todayFocusMinutes",
        "totalFocusMinutes",
        "completedSessions",
        "unlockedSessions",
        "interruptedSessions",
        "currentStreakDays",
        "lastSessionSummary",
        "lastSessionReflection",
        "dailyTargetMinutes",
        "todayTargetProgress"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'statsChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'targetChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'recordRoutineSession'
        QtMocHelpers::SlotData<void(const QString &, const QString &, int, const QString &, const QDateTime &, const QDateTime &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 6 }, { QMetaType::Int, 7 }, { QMetaType::QString, 8 },
            { QMetaType::QDateTime, 9 }, { QMetaType::QDateTime, 10 },
        }}),
        // Slot 'recordRoutineSessionProgress'
        QtMocHelpers::SlotData<void(const QString &, const QString &, int, const QDateTime &, const QDateTime &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 6 }, { QMetaType::Int, 12 }, { QMetaType::QDateTime, 9 },
            { QMetaType::QDateTime, 13 },
        }}),
        // Method 'recordLastSessionReflection'
        QtMocHelpers::MethodData<void(const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'dailyStats'
        QtMocHelpers::PropertyData<QVariantList>(16, 0x80000000 | 17, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'focusHistory'
        QtMocHelpers::PropertyData<QVariantList>(18, 0x80000000 | 17, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'todayFocusMinutes'
        QtMocHelpers::PropertyData<int>(19, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'totalFocusMinutes'
        QtMocHelpers::PropertyData<int>(20, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'completedSessions'
        QtMocHelpers::PropertyData<int>(21, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'unlockedSessions'
        QtMocHelpers::PropertyData<int>(22, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'interruptedSessions'
        QtMocHelpers::PropertyData<int>(23, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'currentStreakDays'
        QtMocHelpers::PropertyData<int>(24, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'lastSessionSummary'
        QtMocHelpers::PropertyData<QString>(25, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'lastSessionReflection'
        QtMocHelpers::PropertyData<QString>(26, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'dailyTargetMinutes'
        QtMocHelpers::PropertyData<int>(27, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'todayTargetProgress'
        QtMocHelpers::PropertyData<double>(28, QMetaType::Double, QMC::DefaultPropertyFlags, 0),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<StatsStore, qt_meta_tag_ZN10StatsStoreE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject StatsStore::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10StatsStoreE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10StatsStoreE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10StatsStoreE_t>.metaTypes,
    nullptr
} };

void StatsStore::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<StatsStore *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->statsChanged(); break;
        case 1: _t->targetChanged(); break;
        case 2: _t->recordRoutineSession((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[6]))); break;
        case 3: _t->recordRoutineSessionProgress((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[5]))); break;
        case 4: _t->recordLastSessionReflection((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (StatsStore::*)()>(_a, &StatsStore::statsChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (StatsStore::*)()>(_a, &StatsStore::targetChanged, 1))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QVariantList*>(_v) = _t->dailyStats(); break;
        case 1: *reinterpret_cast<QVariantList*>(_v) = _t->focusHistory(); break;
        case 2: *reinterpret_cast<int*>(_v) = _t->todayFocusMinutes(); break;
        case 3: *reinterpret_cast<int*>(_v) = _t->totalFocusMinutes(); break;
        case 4: *reinterpret_cast<int*>(_v) = _t->completedSessions(); break;
        case 5: *reinterpret_cast<int*>(_v) = _t->unlockedSessions(); break;
        case 6: *reinterpret_cast<int*>(_v) = _t->interruptedSessions(); break;
        case 7: *reinterpret_cast<int*>(_v) = _t->currentStreakDays(); break;
        case 8: *reinterpret_cast<QString*>(_v) = _t->lastSessionSummary(); break;
        case 9: *reinterpret_cast<QString*>(_v) = _t->lastSessionReflection(); break;
        case 10: *reinterpret_cast<int*>(_v) = _t->dailyTargetMinutes(); break;
        case 11: *reinterpret_cast<double*>(_v) = _t->todayTargetProgress(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 10: _t->setDailyTargetMinutes(*reinterpret_cast<int*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *StatsStore::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *StatsStore::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10StatsStoreE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int StatsStore::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void StatsStore::statsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void StatsStore::targetChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
