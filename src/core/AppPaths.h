#pragma once

#include <QString>

// Single source of truth for FocusOS runtime paths under ~/.focusos.
//
// This deliberately has no dependencies on any service. The stores
// (StatsStore, MusicEngine, InspirationStore, NotesStore, Updater, …) used to
// reach into the 400-line RoutineManager header just to call its static
// dataDirectory() — and two of them shipped their own copies of that path
// logic that silently diverged from RoutineManager's on macOS (they skipped
// the sudo/console-home resolution). Centralising the path here decouples the
// stores from the god header and removes that divergence.
namespace AppPaths {

// The ~/.focusos runtime directory. macOS-aware: when FocusOS runs elevated
// (sudo), this resolves the invoking console user's home rather than root's,
// so privileged and unprivileged runs agree on one data directory.
QString dataDirectory();

// dataDirectory() joined with a relative path, e.g. filePath("stats.json").
QString filePath(const QString &relative);

} // namespace AppPaths
