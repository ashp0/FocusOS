#pragma once

#include <QObject>

class TOTPEngine final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString secret READ secret NOTIFY secretChanged)
    Q_PROPERTY(QString enrollmentUri READ enrollmentUri NOTIFY secretChanged)
    Q_PROPERTY(QString qrCodeDataUrl READ qrCodeDataUrl NOTIFY secretChanged)
    Q_PROPERTY(bool firstLaunch READ firstLaunch NOTIFY secretChanged)
    // True when the user has already enrolled an authenticator but the on-disk
    // secret is missing/empty/corrupt. We deliberately do NOT mint a new secret
    // in that case (it would lock the user out — their app holds the old one);
    // the UI shows a recovery panel that lets them paste the secret they saved.
    Q_PROPERTY(bool secretMissing READ secretMissing NOTIFY secretChanged)

public:
    explicit TOTPEngine(QObject *parent = nullptr);

    QString secret() const;
    QString enrollmentUri() const;
    QString qrCodeDataUrl() const;
    bool firstLaunch() const;
    bool secretMissing() const;

    Q_INVOKABLE bool validate(const QString &code) const;
    Q_INVOKABLE void completeFirstLaunchEnrollment();
    Q_INVOKABLE void resetSecret();
    // Recovery: adopt a base32 secret the user saved elsewhere, but ONLY if the
    // supplied current code validates against it (so a typo'd paste can't silently
    // overwrite). Returns true and re-arms enrollment on success. Spaces in the
    // pasted secret are ignored.
    Q_INVOKABLE bool restoreSecret(const QString &base32Secret, const QString &code);
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
    bool m_secretMissing = false;
};
