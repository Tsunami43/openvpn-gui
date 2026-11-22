#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// Looks at the latest release of the public GitHub repository and says when
// it is newer than this build. Installing it is pacman's job (the package
// comes from the project's pacman repository), so this only tells.
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    struct Options {
        QUrl releaseApi; // empty: the latest release of Tsunami43/openvpn-gui
        int checkEveryMs = 6 * 60 * 60 * 1000;
    };

    explicit Updater(const QString &currentVersion, QObject *parent = nullptr)
        : Updater(currentVersion, Options(), parent) {}
    Updater(const QString &currentVersion, const Options &options, QObject *parent = nullptr);

    QString currentVersion() const { return m_current; }
    QString latestVersion() const { return m_latest; }
    bool available() const { return !m_latest.isEmpty() && isNewer(m_latest, m_current); }
    QUrl releasePage() const { return m_pageUrl; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void openReleasePage() const;

    // 0.10.0 is newer than 0.9.2; a leading v and a -suffix are ignored.
    static bool isNewer(const QString &candidate, const QString &current);

signals:
    void changed();

private:
    void releaseFetched(QNetworkReply *reply);

    QString m_current, m_latest;
    QUrl m_api, m_pageUrl;
    bool m_checking = false;
    QNetworkAccessManager m_network;
    QTimer m_timer;
};
