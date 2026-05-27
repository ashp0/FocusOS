#pragma once

#include <QObject>

class TOTPEngine final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString secret READ secret NOTIFY secretChanged)
    Q_PROPERTY(QString enrollmentUri READ enrollmentUri NOTIFY secretChanged)
    Q_PROPERTY(QString qrCodeDataUrl READ qrCodeDataUrl NOTIFY secretChanged)
    Q_PROPERTY(bool firstLaunch READ firstLaunch NOTIFY secretChanged)

public:
    explicit TOTPEngine(QObject *parent = nullptr);

    QString secret() const;
    QString enrollmentUri() const;
    QString qrCodeDataUrl() const;
    bool firstLaunch() const;

    Q_INVOKABLE bool validate(const QString &code) const;
    Q_INVOKABLE void completeFirstLaunchEnrollment();
    Q_INVOKABLE void resetSecret();
    static QString generateCode(const QString &base32Secret, quint64 timeStep, int digits = 6);

signals:
    void secretChanged();

private:
    QString generateSecret() const;
    void persistSecret();
    void ensureSecret();
    void rebuildQrCode();
    QString codeForStep(quint64 timeStep) const;

    QString m_secret;
    QString m_enrollmentUri;
    QString m_qrCodeDataUrl;
    bool m_firstLaunch = false;
};
