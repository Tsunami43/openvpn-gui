#include "updater.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSysInfo>

namespace {
QList<int> parts(QString version) {
    if (version.startsWith('v') || version.startsWith('V'))
        version.remove(0, 1);
    version = version.section('-', 0, 0);
    QList<int> numbers;
    for (const QString &piece : version.split('.'))
        numbers << piece.toInt();
    return numbers;
}
} // namespace

Updater::Updater(const QString &currentVersion, const Options &options, QObject *parent)
    : QObject(parent), m_current(currentVersion) {
    m_api = options.releaseApi.isEmpty()
        ? QUrl("https://api.github.com/repos/Tsunami43/openvpn-gui/releases/latest")
        : options.releaseApi;
    m_downloadDir = options.downloadDir;
    if (m_downloadDir.isEmpty()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (base.isEmpty())
            base = QDir::tempPath();
        m_downloadDir = base + "/openvpn-gui/update";
    }
    m_pkexec = options.pkexec.isEmpty() ? QStandardPaths::findExecutable("pkexec") : options.pkexec;
    m_pacman = options.pacman.isEmpty() ? QStandardPaths::findExecutable("pacman") : options.pacman;
    m_arch = options.arch;
    if (m_arch.isEmpty()) {
        m_arch = QSysInfo::currentCpuArchitecture();
        if (m_arch == "arm64")
            m_arch = "aarch64";
    }

    connect(&m_pacmanRun, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(m_pacmanRun.readAllStandardError()).trimmed();
        QFile::remove(m_downloadDir + "/" + m_packageName);
        if (status == QProcess::NormalExit && code == 0) {
            setState("idle");
            emit installed();
        } else if (status == QProcess::NormalExit && code == 126) {
            fail(tr("The password prompt was dismissed, so the update was not installed."));
        } else {
            fail(tr("The update could not be installed.") + (output.isEmpty() ? QString() : "\n\n" + output));
        }
    });
    connect(&m_pacmanRun, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("pkexec could not be run: %1").arg(m_pacmanRun.errorString()));
    });

    // A first look shortly after start, then now and again while running.
    m_timer.setInterval(options.checkEveryMs);
    connect(&m_timer, &QTimer::timeout, this, &Updater::check);
    if (options.checkEveryMs > 0) {
        m_timer.start();
        QTimer::singleShot(3000, this, &Updater::check);
    }
}

bool Updater::isNewer(const QString &candidate, const QString &current) {
    const QList<int> a = parts(candidate), b = parts(current);
    for (qsizetype i = 0; i < qMax(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

void Updater::setState(const QString &state) {
    m_state = state;
    emit changed();
}

void Updater::fail(const QString &message) {
    m_error = message;
    m_progress = 0;
    setState("idle");
}

void Updater::clearError() {
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit changed();
}

void Updater::check() {
    if (m_state != "idle")
        return;
    setState("checking");
    QNetworkRequest request(m_api);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "openvpn-gui/" + m_current);
    m_reply = m_network.get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { releaseFetched(reply); });
}

void Updater::releaseFetched(QNetworkReply *reply) {
    reply->deleteLater();
    // A failed look is quiet: no network now is no reason to bother anyone.
    setState("idle");
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = release.value("tag_name").toString();
    if (tag.isEmpty() || release.value("draft").toBool() || release.value("prerelease").toBool())
        return;
    m_pageUrl = QUrl(release.value("html_url").toString());
    m_packageUrl.clear();
    m_packageName.clear();
    for (const QJsonValue &value : release.value("assets").toArray()) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value("name").toString();
        if (name.contains("-debug-") || !name.contains("-" + m_arch + ".pkg.tar."))
            continue;
        m_packageName = QFileInfo(name).fileName();
        m_packageUrl = QUrl(asset.value("browser_download_url").toString());
        break;
    }
    m_latest = tag.startsWith('v') ? tag.mid(1) : tag;
    emit changed();
}

void Updater::install() {
    if (m_state != "idle" || !available())
        return;
    m_error.clear();
    if (m_packageUrl.isEmpty()) {
        // Nothing built for this machine: the release page says what to do.
        QDesktopServices::openUrl(m_pageUrl);
        emit changed();
        return;
    }
    if (m_pkexec.isEmpty() || m_pacman.isEmpty()) {
        fail(tr("Updating needs pkexec and pacman."));
        return;
    }
    m_progress = 0;
    setState("downloading");
    QNetworkRequest request(m_packageUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "openvpn-gui/" + m_current);
    m_reply = m_network.get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
        if (total > 0) {
            m_progress = double(done) / double(total);
            emit changed();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { packageFetched(reply); });
}

void Updater::packageFetched(QNetworkReply *reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("The update could not be downloaded: %1").arg(reply->errorString()));
        return;
    }
    QDir().mkpath(m_downloadDir);
    QFile::setPermissions(m_downloadDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QString path = m_downloadDir + "/" + m_packageName;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(reply->readAll()) < 0) {
        fail(tr("The update could not be saved to %1.").arg(path));
        return;
    }
    file.close();
    m_progress = 1;
    setState("installing");
    m_pacmanRun.start(m_pkexec, {m_pacman, "-U", "--noconfirm", path});
}
