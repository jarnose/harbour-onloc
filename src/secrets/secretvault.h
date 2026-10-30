#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <functional>

namespace Sailfish {
namespace Secrets {
class SecretManager;
}
}

// Thin async wrapper around the Sailfish Secrets API. The refresh token
// are stored as CollectionSecrets in a single device-lock-protected,
// block-encrypted (sqlcipher) collection - never written to plain disk.
//
// Callbacks are invoked once, on the calling thread's event loop, when the
// underlying Sailfish::Secrets::Request finishes. SecretVault is intended to
// live for the app's lifetime (owned by AppController), so callbacks are not
// guarded against the vault itself being destroyed mid-request.
//
// Note: a sandboxed app needs the "Secrets" Sailjail permission (see
// harbour-onloc.desktop) just to reach the daemon at all - without it every
// request fails with Result::SecretManagerNotInitializedError regardless of
// timing, which looks like (but isn't) a startup race.
class SecretVault : public QObject
{
    Q_OBJECT
public:
    using ResultCallback = std::function<void(bool ok, const QString &error)>;
    using DataCallback = std::function<void(bool ok, const QByteArray &data, const QString &error)>;

    explicit SecretVault(QObject *parent = nullptr);
    ~SecretVault() override;

    // Creates the vault collection if it doesn't already exist. Safe to call
    // every time the app starts; "already exists" counts as success.
    void ensureCollection(ResultCallback callback);

    void storeSecret(const QString &name, const QByteArray &data, ResultCallback callback);
    void loadSecret(const QString &name, DataCallback callback);
    void deleteSecret(const QString &name, ResultCallback callback);

private:
    Sailfish::Secrets::SecretManager *m_manager;
    static const QString CollectionName;
};
