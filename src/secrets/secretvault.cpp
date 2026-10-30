#include "secretvault.h"

#include <Secrets/secretmanager.h>
#include <Secrets/secret.h>
#include <Secrets/createcollectionrequest.h>
#include <Secrets/storesecretrequest.h>
#include <Secrets/storedsecretrequest.h>
#include <Secrets/deletesecretrequest.h>
#include <Secrets/result.h>

using namespace Sailfish::Secrets;

const QString SecretVault::CollectionName = QStringLiteral("OnlocSecrets");

SecretVault::SecretVault(QObject *parent)
    : QObject(parent)
    , m_manager(new SecretManager(this))
{
}

SecretVault::~SecretVault() = default;

void SecretVault::ensureCollection(ResultCallback callback)
{
    auto *req = new CreateCollectionRequest(this);
    req->setManager(m_manager);
    req->setCollectionName(CollectionName);
    req->setAccessControlMode(SecretManager::OwnerOnlyMode);
    req->setCollectionLockType(CreateCollectionRequest::DeviceLock);
    // Once the device has been unlocked, keep the vault available without
    // repeated prompts - matches how users expect an authenticator app to
    // behave (other authenticator apps don't re-prompt per code view).
    req->setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    req->setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
    req->setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);
    req->setUserInteractionMode(SecretManager::SystemInteraction);

    connect(req, &Request::statusChanged, req, [req, callback]() {
        if (req->status() != Request::Finished)
            return;
        const Result result = req->result();
        const bool ok = result.code() != Result::Failed
                || result.errorCode() == Result::CollectionAlreadyExistsError;
        const QString error = ok ? QString() : result.errorMessage();
        req->deleteLater();
        callback(ok, error);
    });
    req->startRequest();
}

void SecretVault::storeSecret(const QString &name, const QByteArray &data, ResultCallback callback)
{
    Secret secret(Secret::Identifier(name, CollectionName,
                                      SecretManager::DefaultEncryptedStoragePluginName));
    secret.setData(data);
    secret.setType(Secret::TypeBlob);

    auto *req = new StoreSecretRequest(this);
    req->setManager(m_manager);
    req->setSecretStorageType(StoreSecretRequest::CollectionSecret);
    // Must be SystemInteraction, not PreventInteraction: tried
    // PreventInteraction on the theory that DeviceLockKeepUnlocked should
    // make the collection stay unlocked so ordinary requests would never
    // need to prompt - but on device the collection is apparently locked
    // far more often than that semantic implies (most likely because
    // sailfish-secretsd's own crash-restart cycle, see below, resets
    // whatever keeps it unlocked), so PreventInteraction just made every
    // request fail outright with "collection is locked". SystemInteraction
    // does carry a real, confirmed platform bug - sailfish-secretsd's
    // PasswordAgentPlugin::cancelAuthentication() can segfault the daemon
    // if this app disconnects while its polkit confirmation is still in
    // flight - but the alternative doesn't work at all right now.
    req->setUserInteractionMode(SecretManager::SystemInteraction);
    req->setSecret(secret);

    connect(req, &Request::statusChanged, req, [req, callback]() {
        if (req->status() != Request::Finished)
            return;
        const Result result = req->result();
        const bool ok = result.code() != Result::Failed;
        const QString error = ok ? QString() : result.errorMessage();
        req->deleteLater();
        callback(ok, error);
    });
    req->startRequest();
}

void SecretVault::loadSecret(const QString &name, DataCallback callback)
{
    auto *req = new StoredSecretRequest(this);
    req->setManager(m_manager);
    req->setIdentifier(Secret::Identifier(name, CollectionName,
                                           SecretManager::DefaultEncryptedStoragePluginName));
    // See the comment in storeSecret() for why this is SystemInteraction.
    req->setUserInteractionMode(SecretManager::SystemInteraction);

    connect(req, &Request::statusChanged, req, [req, callback]() {
        if (req->status() != Request::Finished)
            return;
        const Result result = req->result();
        const bool ok = result.code() != Result::Failed;
        const QByteArray data = ok ? req->secret().data() : QByteArray();
        const QString error = ok ? QString() : result.errorMessage();
        req->deleteLater();
        callback(ok, data, error);
    });
    req->startRequest();
}

void SecretVault::deleteSecret(const QString &name, ResultCallback callback)
{
    auto *req = new DeleteSecretRequest(this);
    req->setManager(m_manager);
    req->setIdentifier(Secret::Identifier(name, CollectionName,
                                           SecretManager::DefaultEncryptedStoragePluginName));
    // See the comment in storeSecret() for why this is SystemInteraction.
    req->setUserInteractionMode(SecretManager::SystemInteraction);

    connect(req, &Request::statusChanged, req, [req, callback]() {
        if (req->status() != Request::Finished)
            return;
        const Result result = req->result();
        const bool ok = result.code() != Result::Failed;
        const QString error = ok ? QString() : result.errorMessage();
        req->deleteLater();
        callback(ok, error);
    });
    req->startRequest();
}
