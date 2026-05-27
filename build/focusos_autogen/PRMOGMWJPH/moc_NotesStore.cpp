/****************************************************************************
** Meta object code from reading C++ file 'NotesStore.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/NotesStore.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'NotesStore.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10NotesStoreE_t {};
} // unnamed namespace

template <> constexpr inline auto NotesStore::qt_create_metaobjectdata<qt_meta_tag_ZN10NotesStoreE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "NotesStore",
        "textChanged",
        "",
        "draftChanged",
        "archiveChanged",
        "onRoutineEngaged",
        "routineId",
        "routineName",
        "onRoutineSessionFinished",
        "minutes",
        "result",
        "startedAt",
        "endedAt",
        "sessionNoteText",
        "sessionId",
        "sessionNote",
        "QVariantMap",
        "text",
        "draftRoutineName",
        "todayCombinedNotes",
        "todayNoteCount",
        "sessionHistory",
        "QVariantList"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'textChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'draftChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'archiveChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onRoutineEngaged'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 }, { QMetaType::QString, 7 },
        }}),
        // Slot 'onRoutineSessionFinished'
        QtMocHelpers::SlotData<void(const QString &, const QString &, int, const QString &, const QDateTime &, const QDateTime &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 }, { QMetaType::QString, 7 }, { QMetaType::Int, 9 }, { QMetaType::QString, 10 },
            { QMetaType::QDateTime, 11 }, { QMetaType::QDateTime, 12 },
        }}),
        // Method 'sessionNoteText'
        QtMocHelpers::MethodData<QString(const QString &) const>(13, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 14 },
        }}),
        // Method 'sessionNote'
        QtMocHelpers::MethodData<QVariantMap(const QString &) const>(15, 2, QMC::AccessPublic, 0x80000000 | 16, {{
            { QMetaType::QString, 14 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'text'
        QtMocHelpers::PropertyData<QString>(17, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'draftRoutineName'
        QtMocHelpers::PropertyData<QString>(18, QMetaType::QString, QMC::DefaultPropertyFlags, 1),
        // property 'todayCombinedNotes'
        QtMocHelpers::PropertyData<QString>(19, QMetaType::QString, QMC::DefaultPropertyFlags, 2),
        // property 'todayNoteCount'
        QtMocHelpers::PropertyData<int>(20, QMetaType::Int, QMC::DefaultPropertyFlags, 2),
        // property 'sessionHistory'
        QtMocHelpers::PropertyData<QVariantList>(21, 0x80000000 | 22, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 2),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<NotesStore, qt_meta_tag_ZN10NotesStoreE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject NotesStore::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10NotesStoreE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10NotesStoreE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10NotesStoreE_t>.metaTypes,
    nullptr
} };

void NotesStore::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<NotesStore *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->textChanged(); break;
        case 1: _t->draftChanged(); break;
        case 2: _t->archiveChanged(); break;
        case 3: _t->onRoutineEngaged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 4: _t->onRoutineSessionFinished((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[6]))); break;
        case 5: { QString _r = _t->sessionNoteText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 6: { QVariantMap _r = _t->sessionNote((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (NotesStore::*)()>(_a, &NotesStore::textChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (NotesStore::*)()>(_a, &NotesStore::draftChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (NotesStore::*)()>(_a, &NotesStore::archiveChanged, 2))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->text(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->draftRoutineName(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->todayCombinedNotes(); break;
        case 3: *reinterpret_cast<int*>(_v) = _t->todayNoteCount(); break;
        case 4: *reinterpret_cast<QVariantList*>(_v) = _t->sessionHistory(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setText(*reinterpret_cast<QString*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *NotesStore::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *NotesStore::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10NotesStoreE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int NotesStore::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void NotesStore::textChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void NotesStore::draftChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void NotesStore::archiveChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
