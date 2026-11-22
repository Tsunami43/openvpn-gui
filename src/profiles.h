#pragma once

#include <QList>
#include <QString>

struct Profile {
    QString id;
    QString name;
    QString config;    // our own copy of the .ovpn
    QString sourceDir; // where the original lived, for files it refers to
    QString username;
    QString host;      // the first "remote", for showing on the card
};

// Saved profiles: a JSON list in the config directory and a copy of each
// imported .ovpn in the data directory. The user's original file is only ever
// read, so editing or deleting it later does not break the profile.
class ProfileStore {
public:
    ProfileStore(const QString &listFile, const QString &dataDir);

    const QList<Profile> &all() const { return m_profiles; }
    const Profile *find(const QString &id) const;

    // Creates the profile when its id is empty. A non-empty importPath (re)reads
    // the .ovpn from there. Returns the saved id, or empty with error set.
    QString save(Profile profile, const QString &importPath, QString *error);
    bool remove(const QString &id, QString *error);

    static QString hostOf(const QString &configText);

private:
    void load();
    bool write(QString *error) const;

    QString m_listFile, m_dataDir;
    QList<Profile> m_profiles;
};
