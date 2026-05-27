#include "core/TOTPEngine.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QPainter>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {

constexpr int QrSize = 37;
constexpr int QrVersion5LDataCodewords = 108;
constexpr int QrVersion5LEccCodewords = 26;

QString focusDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.focusos");
}

QString secretPath()
{
    return focusDataDir() + QStringLiteral("/secret.totp");
}

QString enrollmentPath()
{
    return focusDataDir() + QStringLiteral("/totp.enrolled");
}

QString base32Encode(const QByteArray &bytes)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    QString output;
    int buffer = 0;
    int bitsLeft = 0;
    for (const uchar byte : bytes) {
        buffer = (buffer << 8) | byte;
        bitsLeft += 8;
        while (bitsLeft >= 5) {
            output.append(QLatin1Char(alphabet[(buffer >> (bitsLeft - 5)) & 0x1f]));
            bitsLeft -= 5;
        }
    }
    if (bitsLeft > 0) {
        output.append(QLatin1Char(alphabet[(buffer << (5 - bitsLeft)) & 0x1f]));
    }
    return output;
}

QByteArray base32Decode(const QString &input)
{
    QByteArray output;
    int buffer = 0;
    int bitsLeft = 0;
    for (const QChar character : input.trimmed().toUpper()) {
        int value = -1;
        if (character >= QLatin1Char('A') && character <= QLatin1Char('Z')) {
            value = character.unicode() - 'A';
        } else if (character >= QLatin1Char('2') && character <= QLatin1Char('7')) {
            value = character.unicode() - '2' + 26;
        } else if (character == QLatin1Char('=')) {
            continue;
        }

        if (value < 0) {
            continue;
        }
        buffer = (buffer << 5) | value;
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            output.append(static_cast<char>((buffer >> (bitsLeft - 8)) & 0xff));
            bitsLeft -= 8;
        }
    }
    return output;
}

int gfMultiply(int x, int y)
{
    int result = 0;
    while (y != 0) {
        if ((y & 1) != 0) {
            result ^= x;
        }
        x <<= 1;
        if ((x & 0x100) != 0) {
            x ^= 0x11d;
        }
        y >>= 1;
    }
    return result & 0xff;
}

QVector<int> reedSolomonGenerator(int degree)
{
    QVector<int> generator {1};
    int root = 1;
    for (int i = 0; i < degree; ++i) {
        QVector<int> next(generator.size() + 1);
        for (int j = 0; j < generator.size(); ++j) {
            next[j] ^= gfMultiply(generator[j], root);
            next[j + 1] ^= generator[j];
        }
        generator = next;
        root = gfMultiply(root, 2);
    }
    return generator;
}

QVector<int> reedSolomonRemainder(const QVector<int> &data, int degree)
{
    const QVector<int> generator = reedSolomonGenerator(degree);
    QVector<int> result(degree);

    for (const int value : data) {
        const int factor = value ^ result.first();
        for (int i = 0; i < degree - 1; ++i) {
            result[i] = result[i + 1];
        }
        result[degree - 1] = 0;
        for (int i = 0; i < degree; ++i) {
            result[i] ^= gfMultiply(generator[i + 1], factor);
        }
    }

    return result;
}

class FixedQrCode
{
public:
    explicit FixedQrCode(const QByteArray &payload)
    {
        drawFunctionPatterns();
        const QVector<int> dataCodewords = encodePayload(payload);
        QVector<int> allCodewords = dataCodewords;
        allCodewords += reedSolomonRemainder(dataCodewords, QrVersion5LEccCodewords);
        drawCodewords(allCodewords);
        drawFormatBits();
    }

    QImage toImage(int scale = 8, int quiet = 4) const
    {
        const int pixelSize = (QrSize + quiet * 2) * scale;
        QImage image(pixelSize, pixelSize, QImage::Format_RGB32);
        image.fill(QColor(QStringLiteral("#e8dcc8")));

        QPainter painter(&image);
        painter.fillRect(image.rect(), QColor(QStringLiteral("#e8dcc8")));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#050508")));
        for (int row = 0; row < QrSize; ++row) {
            for (int col = 0; col < QrSize; ++col) {
                if (m_modules[row][col]) {
                    painter.drawRect((col + quiet) * scale, (row + quiet) * scale, scale, scale);
                }
            }
        }
        return image;
    }

private:
    QVector<int> encodePayload(const QByteArray &payload) const
    {
        QVector<bool> bits;
        appendBits(bits, 0b0100, 4);
        appendBits(bits, payload.size(), 8);
        for (const uchar byte : payload) {
            appendBits(bits, byte, 8);
        }

        const int capacityBits = QrVersion5LDataCodewords * 8;
        const int terminator = qMin(4, capacityBits - bits.size());
        appendBits(bits, 0, terminator);
        while ((bits.size() % 8) != 0) {
            bits.append(false);
        }

        QVector<int> data;
        data.reserve(QrVersion5LDataCodewords);
        for (int i = 0; i < bits.size(); i += 8) {
            int value = 0;
            for (int j = 0; j < 8; ++j) {
                value = (value << 1) | (bits[i + j] ? 1 : 0);
            }
            data.append(value);
        }

        bool useFirstPad = true;
        while (data.size() < QrVersion5LDataCodewords) {
            data.append(useFirstPad ? 0xec : 0x11);
            useFirstPad = !useFirstPad;
        }
        return data;
    }

    void appendBits(QVector<bool> &bits, int value, int width) const
    {
        for (int i = width - 1; i >= 0; --i) {
            bits.append(((value >> i) & 1) != 0);
        }
    }

    void setFunctionModule(int row, int col, bool dark)
    {
        if (row < 0 || row >= QrSize || col < 0 || col >= QrSize) {
            return;
        }
        m_modules[row][col] = dark;
        m_function[row][col] = true;
    }

    void drawFinderPattern(int row, int col)
    {
        for (int y = -1; y <= 7; ++y) {
            for (int x = -1; x <= 7; ++x) {
                const int r = row + y;
                const int c = col + x;
                if (r < 0 || r >= QrSize || c < 0 || c >= QrSize) {
                    continue;
                }
                const bool inPattern = x >= 0 && x <= 6 && y >= 0 && y <= 6;
                const bool dark = inPattern && (x == 0 || x == 6 || y == 0 || y == 6 || (x >= 2 && x <= 4 && y >= 2 && y <= 4));
                setFunctionModule(r, c, dark);
            }
        }
    }

    void drawAlignmentPattern(int centerRow, int centerCol)
    {
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                const bool dark = qMax(qAbs(x), qAbs(y)) != 1;
                setFunctionModule(centerRow + y, centerCol + x, dark);
            }
        }
    }

    void drawFunctionPatterns()
    {
        drawFinderPattern(0, 0);
        drawFinderPattern(0, QrSize - 7);
        drawFinderPattern(QrSize - 7, 0);

        for (int i = 8; i < QrSize - 8; ++i) {
            const bool dark = (i % 2) == 0;
            setFunctionModule(6, i, dark);
            setFunctionModule(i, 6, dark);
        }

        drawAlignmentPattern(30, 30);
        drawFormatBits();
        setFunctionModule(QrSize - 8, 8, true);
    }

    bool mask(int row, int col) const
    {
        return ((row + col) % 2) == 0;
    }

    void drawCodewords(const QVector<int> &codewords)
    {
        QVector<bool> bits;
        bits.reserve(codewords.size() * 8);
        for (const int codeword : codewords) {
            appendBits(bits, codeword, 8);
        }

        int bitIndex = 0;
        int upward = 1;
        for (int right = QrSize - 1; right >= 1; right -= 2) {
            if (right == 6) {
                --right;
            }
            for (int vert = 0; vert < QrSize; ++vert) {
                const int row = upward ? (QrSize - 1 - vert) : vert;
                for (int j = 0; j < 2; ++j) {
                    const int col = right - j;
                    if (m_function[row][col]) {
                        continue;
                    }
                    bool dark = false;
                    if (bitIndex < bits.size()) {
                        dark = bits[bitIndex++];
                    }
                    if (mask(row, col)) {
                        dark = !dark;
                    }
                    m_modules[row][col] = dark;
                }
            }
            upward = !upward;
        }
    }

    int formatBits() const
    {
        const int errorCorrectionLevelLow = 1;
        const int maskPattern = 0;
        const int data = (errorCorrectionLevelLow << 3) | maskPattern;
        int remainder = data;
        for (int i = 0; i < 10; ++i) {
            remainder = (remainder << 1) ^ (((remainder >> 9) & 1) != 0 ? 0x537 : 0);
        }
        return ((data << 10) | (remainder & 0x3ff)) ^ 0x5412;
    }

    bool getBit(int value, int index) const
    {
        return ((value >> index) & 1) != 0;
    }

    void drawFormatBits()
    {
        const int bits = formatBits();
        for (int i = 0; i <= 5; ++i) {
            setFunctionModule(8, i, getBit(bits, i));
        }
        setFunctionModule(8, 7, getBit(bits, 6));
        setFunctionModule(8, 8, getBit(bits, 7));
        setFunctionModule(7, 8, getBit(bits, 8));
        for (int i = 9; i < 15; ++i) {
            setFunctionModule(14 - i, 8, getBit(bits, i));
        }

        for (int i = 0; i < 8; ++i) {
            setFunctionModule(QrSize - 1 - i, 8, getBit(bits, i));
        }
        for (int i = 8; i < 15; ++i) {
            setFunctionModule(8, QrSize - 15 + i, getBit(bits, i));
        }
        setFunctionModule(QrSize - 8, 8, true);
    }

    bool m_modules[QrSize][QrSize] = {};
    bool m_function[QrSize][QrSize] = {};
};

} // namespace

TOTPEngine::TOTPEngine(QObject *parent)
    : QObject(parent)
{
    ensureSecret();
    rebuildQrCode();
}

QString TOTPEngine::secret() const
{
    return m_secret;
}

QString TOTPEngine::enrollmentUri() const
{
    return m_enrollmentUri;
}

QString TOTPEngine::qrCodeDataUrl() const
{
    return m_qrCodeDataUrl;
}

bool TOTPEngine::firstLaunch() const
{
    return m_firstLaunch;
}

bool TOTPEngine::validate(const QString &code) const
{
    const QString normalized = code.trimmed();
    if (normalized.size() != 6) {
        return false;
    }

    bool ok = false;
    normalized.toInt(&ok);
    if (!ok) {
        return false;
    }

    const quint64 currentStep = static_cast<quint64>(QDateTime::currentSecsSinceEpoch() / 30);
    for (qint64 drift = -1; drift <= 1; ++drift) {
        const quint64 step = static_cast<quint64>(static_cast<qint64>(currentStep) + drift);
        if (codeForStep(step) == normalized) {
            return true;
        }
    }
    return false;
}

void TOTPEngine::completeFirstLaunchEnrollment()
{
    if (!m_firstLaunch) {
        return;
    }

    m_firstLaunch = false;
    QSaveFile saveFile(enrollmentPath());
    if (saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        saveFile.write("enrolled\n");
        saveFile.commit();
    }
    emit secretChanged();
}

void TOTPEngine::resetSecret()
{
    m_secret = generateSecret();
    m_firstLaunch = false;
    persistSecret();
    QSaveFile saveFile(enrollmentPath());
    if (saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        saveFile.write("enrolled\n");
        saveFile.commit();
    }
    const QString issuer = QStringLiteral("FocusOS");
    const QString label = QStringLiteral("FocusOS");
    m_enrollmentUri = QStringLiteral("otpauth://totp/%1?secret=%2&issuer=%3")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(label)),
             m_secret,
             QString::fromLatin1(QUrl::toPercentEncoding(issuer)));
    rebuildQrCode();
    emit secretChanged();
}

QString TOTPEngine::generateCode(const QString &base32Secret, quint64 timeStep, int digits)
{
    QByteArray counter(8, Qt::Uninitialized);
    for (int i = 7; i >= 0; --i) {
        counter[i] = static_cast<char>(timeStep & 0xff);
        timeStep >>= 8;
    }

    const QByteArray key = base32Decode(base32Secret);
    const QByteArray hmac = QMessageAuthenticationCode::hash(counter, key, QCryptographicHash::Sha1);
    const int offset = hmac.at(hmac.size() - 1) & 0x0f;
    const int binary =
        ((hmac.at(offset) & 0x7f) << 24) |
        ((hmac.at(offset + 1) & 0xff) << 16) |
        ((hmac.at(offset + 2) & 0xff) << 8) |
        (hmac.at(offset + 3) & 0xff);

    const int clampedDigits = qBound(1, digits, 9);
    int modulus = 1;
    for (int i = 0; i < clampedDigits; ++i) {
        modulus *= 10;
    }
    return QStringLiteral("%1").arg(binary % modulus, clampedDigits, 10, QLatin1Char('0'));
}

QString TOTPEngine::generateSecret() const
{
    QByteArray randomBytes;
    randomBytes.resize(20);
    for (char &byte : randomBytes) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return base32Encode(randomBytes);
}

void TOTPEngine::persistSecret()
{
    QDir().mkpath(focusDataDir());
    QSaveFile saveFile(secretPath());
    if (saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        saveFile.write(m_secret.toUtf8());
        saveFile.write("\n");
        saveFile.commit();
    }
}

void TOTPEngine::ensureSecret()
{
    QDir().mkpath(focusDataDir());
    QFile file(secretPath());
    if (file.open(QIODevice::ReadOnly)) {
        m_secret = QString::fromUtf8(file.readAll()).trimmed();
    }

    if (m_secret.isEmpty()) {
        m_secret = generateSecret();
        persistSecret();
    }

    m_firstLaunch = !QFileInfo::exists(enrollmentPath());

    const QString issuer = QStringLiteral("FocusOS");
    const QString label = QStringLiteral("FocusOS");
    m_enrollmentUri = QStringLiteral("otpauth://totp/%1?secret=%2&issuer=%3")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(label)),
             m_secret,
             QString::fromLatin1(QUrl::toPercentEncoding(issuer)));
    emit secretChanged();
}

void TOTPEngine::rebuildQrCode()
{
    const FixedQrCode code(m_enrollmentUri.toUtf8());
    QImage image = code.toImage();
    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    m_qrCodeDataUrl = QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(pngBytes.toBase64()));
}

QString TOTPEngine::codeForStep(quint64 timeStep) const
{
    return generateCode(m_secret, timeStep);
}
