#include "blocker/BlockerHost.h"

#include "blocker/BlockerPolicy.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {

// Shown to the extension; surfaced in the tab-blocked notification. Must match
// the BLOCK_ID the dev host used so policies are interchangeable.
constexpr char kBlockId[] = "FocusOS";

std::mutex g_writeMutex;

void hostLog(const QString &message)
{
    QFile file(BlockerPolicy::blockerDir() + QStringLiteral("/host.log"));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    const QString line = QStringLiteral("[%1] %2\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                                  message);
    file.write(line.toUtf8());
}

// v5 blockListInfo. Active -> one block matching `*` (everything) minus the
// allowlist. Inactive -> no blocks, so the extension lets everything through.
QByteArray buildPolicy(const BlockerPolicy::State &state)
{
    QJsonObject blocks;
    if (state.active) {
        QJsonArray exceptionList;
        for (const QString &host : state.allowlist) {
            exceptionList.append(host);
        }
        QJsonObject block;
        block.insert(QStringLiteral("blockList"), QJsonArray{QStringLiteral("*")});
        block.insert(QStringLiteral("exceptionList"), exceptionList);
        block.insert(QStringLiteral("titleList"), QJsonArray{});
        block.insert(QStringLiteral("allowanceUrlList"), QJsonArray{});
        block.insert(QStringLiteral("lock"), QString());
        block.insert(QStringLiteral("password"), QString());
        block.insert(QStringLiteral("randomTextLength"), 0);
        block.insert(QStringLiteral("allowance"), QString());
        block.insert(QStringLiteral("allowanceRemaining"), QString());
        blocks.insert(QString::fromLatin1(kBlockId), block);
    }

    QJsonObject blockListInfo;
    blockListInfo.insert(QStringLiteral("blocks"), blocks);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 5);
    root.insert(QStringLiteral("isPro"), QStringLiteral("true"));
    root.insert(QStringLiteral("statsEnabled"), QStringLiteral("false"));
    root.insert(QStringLiteral("statsEnabledIncognito"), QStringLiteral("false"));
    root.insert(QStringLiteral("blockListInfo"), blockListInfo);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Native messaging frame: 4-byte little-endian length prefix + payload. stdout
// is reserved for these frames only — everything else goes to host.log.
void sendFrame(const QByteArray &payload)
{
    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    unsigned char header[4] = {
        static_cast<unsigned char>(length & 0xFF),
        static_cast<unsigned char>((length >> 8) & 0xFF),
        static_cast<unsigned char>((length >> 16) & 0xFF),
        static_cast<unsigned char>((length >> 24) & 0xFF),
    };
    std::lock_guard<std::mutex> lock(g_writeMutex);
    std::fwrite(header, 1, sizeof(header), stdout);
    std::fwrite(payload.constData(), 1, static_cast<std::size_t>(payload.size()), stdout);
    std::fflush(stdout);
}

void sendPolicy(const BlockerPolicy::State &state)
{
    sendFrame(buildPolicy(state));
    hostLog(QStringLiteral("pushed policy: active=%1, %2 allowed -> %3")
                .arg(state.active ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(state.allowlist.size())
                .arg(state.allowlist.join(QStringLiteral(", "))));
}

// Read one framed message from stdin into `out`. Returns false on EOF / short
// read (browser closed the port). Content is discarded by the caller.
bool readFrame(QByteArray *out)
{
    unsigned char header[4];
    if (std::fread(header, 1, sizeof(header), stdin) != sizeof(header)) {
        return false;
    }
    const std::uint32_t length = static_cast<std::uint32_t>(header[0])
                                 | (static_cast<std::uint32_t>(header[1]) << 8)
                                 | (static_cast<std::uint32_t>(header[2]) << 16)
                                 | (static_cast<std::uint32_t>(header[3]) << 24);
    out->resize(static_cast<int>(length));
    if (length > 0
        && std::fread(out->data(), 1, length, stdin) != length) {
        return false;
    }
    return true;
}

qint64 policyMtimeMs()
{
    QFileInfo info(BlockerPolicy::policyFilePath());
    return info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
}

// Liveness beacon: rewrite the heartbeat file so its mtime tracks "now". The
// browser only spawns this host while the extension is enabled and connected,
// so a fresh file == the extension is alive and enforcing. The main process
// watches it to detect a disabled/removed extension and clamp the network.
void touchHeartbeat()
{
    QFile file(BlockerPolicy::heartbeatFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
        file.close();
    }
}

void clearHeartbeat()
{
    QFile::remove(BlockerPolicy::heartbeatFilePath());
}

} // namespace

namespace focusos {

int runBlockerHost()
{
    hostLog(QStringLiteral("host started (policy=%1)").arg(BlockerPolicy::policyFilePath()));

    BlockerPolicy::State state = BlockerPolicy::read();
    sendPolicy(state);
    touchHeartbeat();

    std::atomic<bool> stop{false};
    qint64 lastMtime = policyMtimeMs();

    // Poll policy.json; push a fresh frame whenever it changes, so adding a site
    // (or engaging/ending a routine) takes effect without a browser restart.
    // The same loop refreshes the liveness beacon every tick.
    std::thread watcher([&] {
        BlockerPolicy::State current = state;
        while (!stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            touchHeartbeat();
            const qint64 mtime = policyMtimeMs();
            BlockerPolicy::State fresh = BlockerPolicy::read();
            if (mtime != lastMtime || fresh.active != current.active
                || fresh.allowlist != current.allowlist) {
                lastMtime = mtime;
                current = fresh;
                sendPolicy(current);
            }
        }
    });

    // Block on stdin until the browser closes the port; inbound chatter
    // (port-check / counter@ / stats@ / blocked@) needs no reply.
    QByteArray message;
    while (readFrame(&message)) {
        // discard
    }

    stop.store(true);
    if (watcher.joinable()) {
        watcher.join();
    }
    // Port closed (extension disabled / browser quit). Drop the beacon so the
    // main process sees the extension is gone within a couple of poll cycles.
    clearHeartbeat();
    hostLog(QStringLiteral("host stopping (port closed)"));
    return 0;
}

} // namespace focusos
