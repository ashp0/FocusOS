#pragma once

#include <QByteArray>

// Key for the HMAC that signs the on-disk blocker rules (policy.dat). The
// writer (FocusOS, via the platform backends) and the reader (the native host,
// same binary in --native-host mode) share it, so the host can reject a file
// it didn't produce.
//
// LIMITATION: this key ships inside the binary. A determined user with a
// disassembler can recover it and forge a policy. That is acceptable here — the
// bar is "not trivially editable in a text editor," not "resistant to a
// motivated reverse-engineer." The fail-closed check in BlockerPolicy::read
// (a tampered/deleted file during a live routine blocks everything) is the part
// that actually protects a focus session.
namespace BlockerSecret {

inline QByteArray hmacKey()
{
    // Assembled at runtime from fragments so the full key isn't a single
    // grep-able string in the binary.
    static const unsigned char a[] = {0x46, 0x6f, 0x63, 0x75, 0x73, 0x4f, 0x53}; // "FocusOS"
    static const unsigned char b[] = {0x2d, 0x62, 0x6c, 0x6b, 0x2d, 0x76, 0x31}; // "-blk-v1"
    static const unsigned char c[] = {0x9e, 0x47, 0xd2, 0x1a, 0x6b, 0xf0, 0x3c, 0x85,
                                       0x21, 0xae, 0x74, 0xc9, 0x58, 0x0d, 0xb3, 0x6f};
    QByteArray key;
    key.append(reinterpret_cast<const char *>(a), sizeof(a));
    key.append(reinterpret_cast<const char *>(c), sizeof(c));
    key.append(reinterpret_cast<const char *>(b), sizeof(b));
    return key;
}

} // namespace BlockerSecret
