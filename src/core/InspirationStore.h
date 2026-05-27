#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

class InspirationStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList assets READ assets NOTIFY assetsChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)

public:
    explicit InspirationStore(QObject *parent = nullptr);

    QVariantList assets() const;
    QString directory() const;

signals:
    void assetsChanged();

private:
    void scheduleScan();
    void scan();
    void ensureReadme() const;

    QString m_directory;
    QVariantList m_assets;
    QFileSystemWatcher m_watcher;
    QTimer m_scanTimer;
};
