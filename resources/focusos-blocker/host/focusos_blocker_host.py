#!/usr/bin/env python3
"""FocusOS Blocker — native messaging host (dev backstop).

This is the standalone reference host used to prove out the extension before it
is folded into the `focusos` binary (the production host will speak the same
protocol over stdio). Chrome/Brave spawn this process when the extension calls
`chrome.runtime.connectNative('com.focusos.blocker')`.

Behaviour:
  * On connect it pushes a v5 `blockListInfo` policy that blocks the ENTIRE
    internet (`blockList: ["*"]`) except the domains listed in `allowlist.txt`,
    which become the `exceptionList`.
  * It keeps watching `allowlist.txt`; edits are pushed live, so you can add a
    site and reload a tab without restarting the browser.
  * It stays connected until the browser closes the port (stdin EOF).

Native messaging framing: each message is a 4-byte little-endian length prefix
followed by that many bytes of UTF-8 JSON. stdout is reserved for framed
messages ONLY — all logging goes to stderr / the log file.

See README.md for the full protocol notes.
"""

import json
import os
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ALLOWLIST_PATH = os.environ.get("FOCUSOS_ALLOWLIST", os.path.join(HERE, "allowlist.txt"))
LOG_PATH = os.environ.get("FOCUSOS_HOST_LOG", os.path.join(HERE, "host.log"))

# The block id shown to the extension; surfaced in tab-blocked notifications.
BLOCK_ID = "FocusOS"

_write_lock = threading.Lock()


def log(msg):
    try:
        with open(LOG_PATH, "a") as fh:
            fh.write("[{}] {}\n".format(time.strftime("%Y-%m-%d %H:%M:%S"), msg))
    except OSError:
        pass


def read_allowlist():
    """Return a de-duplicated, order-preserving list of allowed domains.

    Lines are trimmed; blank lines and `#` comments are ignored. A leading
    scheme / `www.` is stripped so `https://www.github.com/` and `github.com`
    behave identically — the extension matches on the host + path.
    """
    domains = []
    seen = set()
    try:
        with open(ALLOWLIST_PATH, "r", encoding="utf-8") as fh:
            for raw in fh:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                line = line.split("//", 1)[-1]  # drop scheme
                if line.startswith("www."):
                    line = line[4:]
                line = line.rstrip("/").lower()
                if line and line not in seen:
                    seen.add(line)
                    domains.append(line)
    except FileNotFoundError:
        log("allowlist not found at {}; blocking everything".format(ALLOWLIST_PATH))
    return domains


def build_policy(allowlist):
    """v5 blockListInfo: one block that matches `*` (everything) minus allowlist."""
    return {
        "version": 5,
        "isPro": "true",
        "statsEnabled": "false",
        "statsEnabledIncognito": "false",
        "blockListInfo": {
            "blocks": {
                BLOCK_ID: {
                    "blockList": ["*"],
                    "exceptionList": allowlist,
                    "titleList": [],
                    "allowanceUrlList": [],
                    "lock": "",
                    "password": "",
                    "randomTextLength": 0,
                    "allowance": "",
                    "allowanceRemaining": "",
                }
            }
        },
    }


def send_message(obj):
    data = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    with _write_lock:
        sys.stdout.buffer.write(struct.pack("<I", len(data)))
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()


def read_message():
    """Read one framed message from stdin; return parsed JSON or None on EOF."""
    raw_len = sys.stdin.buffer.read(4)
    if len(raw_len) < 4:
        return None
    msg_len = struct.unpack("<I", raw_len)[0]
    raw = sys.stdin.buffer.read(msg_len)
    if len(raw) < msg_len:
        return None
    try:
        return json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return raw.decode("utf-8", "replace")


def watch_allowlist(state):
    """Poll the allowlist file; push a fresh policy whenever it changes."""
    while not state["stop"]:
        time.sleep(2)
        try:
            mtime = os.path.getmtime(ALLOWLIST_PATH)
        except OSError:
            mtime = 0
        allowlist = read_allowlist()
        if mtime != state["mtime"] or allowlist != state["allowlist"]:
            state["mtime"] = mtime
            state["allowlist"] = allowlist
            send_message(build_policy(allowlist))
            log("pushed policy: {} allowed -> {}".format(len(allowlist), allowlist))


def main():
    log("host started (allowlist={})".format(ALLOWLIST_PATH))
    allowlist = read_allowlist()
    state = {
        "stop": False,
        "mtime": os.path.getmtime(ALLOWLIST_PATH) if os.path.exists(ALLOWLIST_PATH) else 0,
        "allowlist": allowlist,
    }

    # Push the initial policy immediately on connect.
    send_message(build_policy(allowlist))
    log("pushed initial policy: {} allowed -> {}".format(len(allowlist), allowlist))

    watcher = threading.Thread(target=watch_allowlist, args=(state,), daemon=True)
    watcher.start()

    # Block on stdin until the browser closes the port; ignore inbound chatter
    # (port-check, counter@, stats@, blocked@ ...) — none of it needs a reply.
    while True:
        msg = read_message()
        if msg is None:
            break
    state["stop"] = True
    log("host stopping (port closed)")


if __name__ == "__main__":
    main()
