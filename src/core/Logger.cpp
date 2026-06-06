#include "core/Logger.h"

#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcess>
#include <QSysInfo>
#include <QTextStream>

#include <atomic>

namespace {

Logger *g_logger = nullptr;
QtMessageHandler g_chainedHandler = nullptr;
// Re-entrancy guard: writing a log line must never itself trigger logging that
// recurses back into the handler and deadlocks on the mutex.
thread_local bool t_inHandler = false;

char severityTag(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return 'D';
    case QtInfoMsg:
        return 'I';
    case QtWarningMsg:
        return 'W';
    case QtCriticalMsg:
        return 'C';
    case QtFatalMsg:
        return 'F';
    }
    return '?';
}

void focusosMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (g_logger && !t_inHandler) {
        t_inHandler = true;
        g_logger->record(type, context.category, message);
        t_inHandler = false;
    }
    // Keep the prior behaviour (stderr) intact. The first install returns the
    // default handler; if Qt hands back nullptr the default is implicit, so
    // format and print it ourselves rather than calling through a null pointer.
    if (g_chainedHandler) {
        g_chainedHandler(type, context, message);
    } else {
        fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
        fflush(stderr);
    }
}

} // namespace

Logger::Logger(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_file(path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    // A read-only home (or a full disk) just means no file sink; record() guards
    // on isOpen(), so logging degrades to stderr-only rather than failing.
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        qWarning("FocusOS: could not open log file %s", qUtf8Printable(path));
    }
}

void Logger::install()
{
    if (g_logger) {
        return;
    }
    const QString path = AppPaths::dataDirectory() + QStringLiteral("/logs/focusos.log");
    // Parent to the application so it lives for the whole process and is torn
    // down cleanly; the message handler nulls out via the qApp aboutToQuit hook.
    g_logger = new Logger(path, qApp);
    g_chainedHandler = qInstallMessageHandler(focusosMessageHandler);

    // Stop routing to a dangling singleton if the app object is destroyed before
    // static teardown (handler pointers outlive QObject parents otherwise).
    if (qApp) {
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, g_logger, [] {
            g_logger->writeLine(QStringLiteral("──────── FocusOS session ending ────────"));
        });
    }

    g_logger->writeLine(QStringLiteral("════════ FocusOS %1 starting ════════")
                            .arg(QCoreApplication::applicationVersion()));
    g_logger->writeLine(QStringLiteral("  pid %1 · Qt %2 · %3 %4 · %5")
                            .arg(QCoreApplication::applicationPid())
                            .arg(QStringLiteral(QT_VERSION_STR),
                                 QSysInfo::prettyProductName(),
                                 QSysInfo::currentCpuArchitecture(),
                                 QDateTime::currentDateTime().toString(Qt::ISODate)));
}

Logger *Logger::instance()
{
    return g_logger;
}

QString Logger::logFilePath() const
{
    return m_path;
}

QString Logger::logDirectory() const
{
    return QFileInfo(m_path).absolutePath();
}

void Logger::record(QtMsgType type, const char *category, const QString &message)
{
    const QString cat = (category && qstrcmp(category, "default") != 0)
                            ? QString::fromLatin1(category)
                            : QString();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString line = QStringLiteral("%1 [%2] ").arg(stamp).arg(severityTag(type));
    if (!cat.isEmpty()) {
        line += cat + QStringLiteral(": ");
    }
    line += message;
    writeLine(line);
}

void Logger::note(const QString &category, const QString &message)
{
    // Reuse the same pipeline; tagged as info so it stands out from warnings.
    record(QtInfoMsg, category.toLatin1().constData(), message);
}

void Logger::writeLine(const QString &line)
{
    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen()) {
        return;
    }
    rotateIfNeeded();
    QByteArray bytes = line.toUtf8();
    bytes.append('\n');
    m_file.write(bytes);
    m_file.flush(); // crash-survival: never lose the last line to a buffer
    locker.unlock();
    emit appended();
}

void Logger::rotateIfNeeded()
{
    // Caller holds m_mutex.
    if (m_file.size() < m_maxBytes) {
        return;
    }
    m_file.close();
    // focusos.log.(keep-1) → drop; then shift each older sibling up one slot.
    const QString base = m_path;
    QFile::remove(base + QStringLiteral(".%1").arg(m_keep));
    for (int i = m_keep - 1; i >= 1; --i) {
        const QString from = base + QStringLiteral(".%1").arg(i);
        const QString to = base + QStringLiteral(".%1").arg(i + 1);
        if (QFile::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    QFile::remove(base + QStringLiteral(".1"));
    QFile::rename(base, base + QStringLiteral(".1"));
    m_file.setFileName(base);
    // Can't qWarning() here: we hold m_mutex, and the handler would re-enter
    // writeLine() and deadlock. A failed reopen is handled by the isOpen() guard.
    (void)m_file.open(QIODevice::WriteOnly | QIODevice::Append);
}

QString Logger::tail(int maxLines) const
{
    QMutexLocker locker(&m_mutex);
    QFile reader(m_path);
    if (!reader.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    // Read only the tail of the file: seek back a bounded window rather than
    // slurping a megabyte every refresh.
    const qint64 total = reader.size();
    const qint64 window = qMin<qint64>(total, 256 * 1024);
    reader.seek(total - window);
    const QString chunk = QString::fromUtf8(reader.readAll());
    locker.unlock();

    QStringList lines = chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (window < total && !lines.isEmpty()) {
        lines.removeFirst(); // first line is probably a partial; drop it
    }
    if (lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);
    }
    return lines.join(QLatin1Char('\n'));
}

void Logger::revealLogs() const
{
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {logDirectory()});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"), {logDirectory()});
#endif
}

bool Logger::clear()
{
    QMutexLocker locker(&m_mutex);
    bool ok = true;
    for (int i = 1; i <= m_keep; ++i) {
        const QString sibling = m_path + QStringLiteral(".%1").arg(i);
        if (QFile::exists(sibling) && !QFile::remove(sibling)) {
            ok = false;
        }
    }
    if (m_file.isOpen()) {
        m_file.resize(0);
        m_file.flush();
    }
    return ok;
}
