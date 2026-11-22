#include "profiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Profiles", text); }
} // namespace

ProfileStore::ProfileStore(const QString &listFile, const QString &dataDir)
    : m_listFile(listFile), m_dataDir(dataDir) {
    load();
}

const Profile *ProfileStore::find(const QString &id) const {
    for (const auto &profile : m_profiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}

QString ProfileStore::hostOf(const QString &configText) {
    static const QRegularExpression remote(R"(^\s*remote\s+(\S+))", QRegularExpression::MultilineOption);
    return remote.match(configText).captured(1);
}

void ProfileStore::load() {
    m_profiles.clear();
    QFile file(m_listFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject object = value.toObject();
        Profile profile;
        profile.id = object.value("id").toString();
        profile.name = object.value("name").toString();
        profile.config = object.value("config").toString();
        profile.sourceDir = object.value("sourceDir").toString();
        profile.username = object.value("username").toString();
        profile.host = object.value("host").toString();
        if (!profile.id.isEmpty())
            m_profiles.append(profile);
    }
}

bool ProfileStore::write(QString *error) const {
    QJsonArray list;
    for (const auto &profile : m_profiles)
        list.append(QJsonObject{{"id", profile.id},
                                {"name", profile.name},
                                {"config", profile.config},
                                {"sourceDir", profile.sourceDir},
                                {"username", profile.username},
                                {"host", profile.host}});
    QDir().mkpath(QFileInfo(m_listFile).absolutePath());
    // QSaveFile writes beside the list and renames over it, so a crash never
    // leaves half a file.
    QSaveFile file(m_listFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(list).toJson()) < 0 || !file.commit()) {
        if (error)
            *error = tr("Profiles could not be saved to %1: %2").arg(m_listFile, file.errorString());
        return false;
    }
    return true;
}

QString ProfileStore::save(Profile profile, const QString &importPath, QString *error) {
    profile.name = profile.name.trimmed();
    profile.username = profile.username.trimmed();
    if (profile.name.isEmpty()) {
        *error = tr("Give the profile a name.");
        return {};
    }
    const bool creating = profile.id.isEmpty();
    if (creating)
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    else if (const Profile *existing = find(profile.id)) {
        profile.config = existing->config;
        profile.sourceDir = existing->sourceDir;
        profile.host = existing->host;
    } else {
        *error = tr("That profile no longer exists.");
        return {};
    }

    const QString source = importPath.trimmed();
    if (creating && source.isEmpty()) {
        *error = tr("Choose the .ovpn file for this profile.");
        return {};
    }
    if (!source.isEmpty() && QFileInfo(source).absoluteFilePath() != profile.config) {
        const QFileInfo info(source);
        QFile original(info.absoluteFilePath());
        if (!info.isFile() || !original.open(QIODevice::ReadOnly)) {
            *error = tr("%1 cannot be read.").arg(info.absoluteFilePath());
            return {};
        }
        const QByteArray contents = original.readAll();
        const QString text = QString::fromUtf8(contents);
        if (!text.contains(QRegularExpression(R"(^\s*(client|remote|dev)\b)", QRegularExpression::MultilineOption))) {
            *error = tr("%1 does not look like an OpenVPN profile.").arg(info.fileName());
            return {};
        }
        const QString target = m_dataDir + "/" + profile.id + ".ovpn";
        QDir().mkpath(m_dataDir);
        QSaveFile copy(target);
        // Profiles often carry keys inline, so only the user may read the copy.
        if (!copy.open(QIODevice::WriteOnly) || copy.write(contents) < 0 || !copy.commit()) {
            *error = tr("The profile could not be copied to %1: %2").arg(target, copy.errorString());
            return {};
        }
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        profile.config = target;
        profile.sourceDir = info.absolutePath();
        profile.host = hostOf(text);
    }

    const QList<Profile> before = m_profiles;
    if (creating) {
        m_profiles.append(profile);
    } else {
        for (auto &existing : m_profiles)
            if (existing.id == profile.id)
                existing = profile;
    }
    if (!write(error)) {
        m_profiles = before;
        return {};
    }
    return profile.id;
}

bool ProfileStore::remove(const QString &id, QString *error) {
    const QList<Profile> before = m_profiles;
    QString config;
    for (qsizetype i = 0; i < m_profiles.size(); ++i)
        if (m_profiles[i].id == id) {
            config = m_profiles[i].config;
            m_profiles.removeAt(i);
            break;
        }
    if (!write(error)) {
        m_profiles = before;
        return false;
    }
    // Only our own copy goes, never a file outside the data directory.
    if (config.startsWith(m_dataDir + "/"))
        QFile::remove(config);
    return true;
}
