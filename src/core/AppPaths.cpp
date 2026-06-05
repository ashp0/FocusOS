#include "core/AppPaths.h"

#include <QStandardPaths>

#if defined(Q_OS_MACOS)
#include <QByteArray>

#include <pwd.h>
#include <unistd.h>
#endif

namespace AppPaths {

namespace {

#if defined(Q_OS_MACOS)
// When running under sudo, QStandardPaths::HomeLocation resolves to root's
// home. FocusOS keeps its data under the real console user's ~/.focusos, so
// recover that from SUDO_USER's passwd entry when elevated.
QString consoleHomePath()
{
    const QByteArray sudoUser = qgetenv("SUDO_USER");
    if (geteuid() == 0 && !sudoUser.isEmpty() && sudoUser != "root") {
        if (const struct passwd *pw = getpwnam(sudoUser.constData())) {
            if (pw->pw_dir && pw->pw_dir[0] != '\0') {
                return QString::fromLocal8Bit(pw->pw_dir);
            }
        }
    }
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}
#endif

} // namespace

QString dataDirectory()
{
#if defined(Q_OS_MACOS)
    return consoleHomePath() + QStringLiteral("/.focusos");
#else
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
           + QStringLiteral("/.focusos");
#endif
}

QString filePath(const QString &relative)
{
    return dataDirectory() + QStringLiteral("/") + relative;
}

} // namespace AppPaths
