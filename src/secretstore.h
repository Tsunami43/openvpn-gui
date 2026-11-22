#pragma once

#include <QHash>
#include <QString>

// Where profile passwords live, behind an interface so tests never touch the
// user's keyring.
class SecretStore {
public:
    virtual ~SecretStore() = default;
    // Returns false and fills error when the password could not be stored.
    virtual bool store(const QString &profileId, const QString &label, const QString &password, QString *error) = 0;
    virtual QString lookup(const QString &profileId) = 0;
    virtual void remove(const QString &profileId) = 0;
};

// The desktop keyring (gnome-keyring on Omarchy) through libsecret, so saved
// passwords are encrypted at rest and unlocked with the login.
class KeyringStore : public SecretStore {
public:
    bool store(const QString &profileId, const QString &label, const QString &password, QString *error) override;
    QString lookup(const QString &profileId) override;
    void remove(const QString &profileId) override;
};

class MemoryStore : public SecretStore {
public:
    bool store(const QString &profileId, const QString &, const QString &password, QString *) override {
        m_secrets.insert(profileId, password);
        return true;
    }
    QString lookup(const QString &profileId) override { return m_secrets.value(profileId); }
    void remove(const QString &profileId) override { m_secrets.remove(profileId); }

private:
    QHash<QString, QString> m_secrets;
};
