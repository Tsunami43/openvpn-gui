#include "updater.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

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

void Updater::check() {
    if (m_checking)
        return;
    m_checking = true;
    QNetworkRequest request(m_api);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "openvpn-gui/" + m_current);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { releaseFetched(reply); });
}

void Updater::releaseFetched(QNetworkReply *reply) {
    reply->deleteLater();
    m_checking = false;
    // A failed look is quiet: no network now is no reason to bother anyone.
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = release.value("tag_name").toString();
    if (!tag.startsWith('v') || release.value("draft").toBool() || release.value("prerelease").toBool())
        return;
    m_pageUrl = QUrl(release.value("html_url").toString());
    m_latest = tag.mid(1);
    emit changed();
}

QString Updater::updateCommand() const {
    return QStandardPaths::findExecutable("omarchy-update").isEmpty() ? "sudo pacman -Syu" : "omarchy update";
}

void Updater::openReleasePage() const {
    if (m_pageUrl.isValid())
        QDesktopServices::openUrl(m_pageUrl);
}
