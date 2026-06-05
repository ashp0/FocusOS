#include "core/FilePicker.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace FilePicker {

AppResult pickApplication()
{
#if defined(Q_OS_MACOS)
    AppResult result;
    result.path = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Select Allowed Application"),
        QStringLiteral("/Applications"),
        QStringLiteral("Applications (*.app)"));
    return result;
#elif defined(Q_OS_LINUX)
    // Apps live in lots of places on Linux: system .desktop files, per-user and
    // Flatpak/Snap exports (under dotted/hidden dirs), and standalone binaries /
    // AppImages anywhere in $HOME. The old picker started in /usr/share and used
    // the native dialog, which hid dotfolders — so apps like Obsidian (Flatpak,
    // or an AppImage in ~/Applications) were unreachable. Use a Qt dialog with
    // hidden files revealed, shortcuts to the common app dirs, and a filter that
    // also matches AppImages / plain executables.
    static const QStringList appDirs {
        QDir::homePath() + QStringLiteral("/.local/share/applications"),
        QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/snapd/desktop/applications"),
        QStringLiteral("/usr/share/applications"),
        QDir::homePath() + QStringLiteral("/Applications"),
    };

    QString startDir = QDir::homePath();
    for (const QString &dir : appDirs) {
        if (QFileInfo::exists(dir)) {
            startDir = dir;
            break;
        }
    }

    QFileDialog dialog(nullptr, QStringLiteral("Select Allowed Application"), startDir);
    dialog.setFileMode(QFileDialog::ExistingFile);
    // Qt-drawn dialog so the hidden-files filter and sidebar shortcuts below are
    // actually honored (the native portal dialog ignores them).
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFilter(dialog.filter() | QDir::Hidden);
    dialog.setNameFilters({
        QStringLiteral("Apps & executables (*.desktop *.AppImage *.appimage *.sh *)"),
        QStringLiteral("Desktop entries (*.desktop)"),
        QStringLiteral("AppImages (*.AppImage *.appimage)"),
        QStringLiteral("All files (*)"),
    });

    QList<QUrl> shortcuts { QUrl::fromLocalFile(QDir::homePath()) };
    for (const QString &dir : appDirs) {
        if (QFileInfo::exists(dir)) {
            shortcuts.append(QUrl::fromLocalFile(dir));
        }
    }
    dialog.setSidebarUrls(shortcuts);

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    const QStringList selected = dialog.selectedFiles();
    const QString path = selected.isEmpty() ? QString() : selected.first();
    const QFileInfo info(path);
    if (!path.isEmpty() &&
        info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) != 0 &&
        !info.isExecutable()) {
        return { QString(),
                 QStringLiteral("THAT FILE ISN'T EXECUTABLE — chmod +x IT, OR TYPE A COMMAND LIKE: flatpak run md.obsidian.Obsidian") };
    }
    return { path, QString() };
#else
    return {};
#endif
}

QString pickFile()
{
    // A generic file picker for the "Open File" workflow. The selected path is
    // added to a routine like any app entry, or opened straight away mid-session;
    // either way the backend hands the file to its default application (PDF
    // reader, image viewer, office suite, video player, …) rather than exec'ing
    // it. Any file type is allowed — no executable check.
    const QString documentFilter =
        QStringLiteral("Books & documents (*.pdf *.epub *.mobi *.azw3 *.djvu *.fb2 *.cbz *.cbr *.chm)");
#if defined(Q_OS_MACOS)
    return QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Open File"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("All files (*);;") + documentFilter);
#elif defined(Q_OS_LINUX)
    const QString documentsDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString startDir = QFileInfo::exists(documentsDir) ? documentsDir : QDir::homePath();

    QFileDialog dialog(nullptr, QStringLiteral("Open File"), startDir);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFilter(dialog.filter() | QDir::Hidden);
    dialog.setNameFilters({ QStringLiteral("All files (*)"), documentFilter });
    dialog.setSidebarUrls({
        QUrl::fromLocalFile(QDir::homePath()),
        QUrl::fromLocalFile(startDir),
    });

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    const QStringList selected = dialog.selectedFiles();
    return selected.isEmpty() ? QString() : selected.first();
#else
    return {};
#endif
}

QString pickFolder()
{
    // Editor-side picker for a routine's optional extra access folder. Qt-drawn so
    // hidden folders are reachable and the start dir is honoured.
    const QString startDir = QDir::homePath();
#if defined(Q_OS_MACOS)
    return QFileDialog::getExistingDirectory(
        nullptr, QStringLiteral("Select Access Folder"), startDir);
#elif defined(Q_OS_LINUX)
    QFileDialog dialog(nullptr, QStringLiteral("Select Access Folder"), startDir);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFilter(dialog.filter() | QDir::Hidden);
    dialog.setSidebarUrls({ QUrl::fromLocalFile(QDir::homePath()) });
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    const QStringList selected = dialog.selectedFiles();
    return selected.isEmpty() ? QString() : selected.first();
#else
    return {};
#endif
}

} // namespace FilePicker
