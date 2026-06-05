#pragma once

#include <QString>

// Native file/folder selection dialogs for the routine editor and the in-session
// "open" workflows. This is a pure UI concern — it knows nothing about routines,
// sessions, or persistence — so it lives outside RoutineManager rather than
// bloating that god object. RoutineManager keeps the thin Q_INVOKABLE wrappers
// QML calls; the dialog plumbing (platform start-dirs, Qt-drawn dialogs that
// reveal dotfolders, sidebar shortcuts) lives here.
namespace FilePicker {

struct AppResult
{
    // Absolute path of the chosen application/executable/desktop entry, or empty
    // if the user cancelled or picked an invalid (non-executable) file.
    QString path;
    // Set when the user picked a regular non-executable file: a user-facing hint
    // the caller should surface as a status message. Empty otherwise.
    QString error;
};

// Picker for a routine's allowed application. Validates the choice is a desktop
// entry or executable; otherwise returns an empty path plus an `error` hint.
AppResult pickApplication();

// Generic "Open File" picker (any file type; opened in its default viewer). Used
// for the editor's "+ OPEN FILE" entries and the in-session "OPEN DOC" action.
QString pickFile();

// Folder picker for a routine's optional extra access folder.
QString pickFolder();

} // namespace FilePicker
