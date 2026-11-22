#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// Looks at the latest release of the public GitHub repository and, when it is
// newer than this build, offers it. Updating downloads the release's package
// for this machine and installs it with pacman through pkexec; a release with
// no package for this machine opens its page in the browser instead.
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // idle, checking, downloading, installing
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    struct Options {
        QUrl releaseApi;    // empty: the latest release of Tsunami43/openvpn-gui
        QString downloadDir; // empty: $XDG_RUNTIME_DIR/openvpn-gui/update
        QString pkexec;     // empty: found on PATH
        QString pacman;     // empty: found on PATH
        QString arch;       // empty: this machine's, in pacman's words
        int checkEveryMs = 6 * 60 * 60 * 1000;
    };

    explicit Updater(const QString &currentVersion, QObject *parent = nullptr)
        : Updater(currentVersion, Options(), parent) {}
    Updater(const QString &currentVersion, const Options &options, QObject *parent = nullptr);

    QString currentVersion() const { return m_current; }
    QString latestVersion() const { return m_latest; }
    bool available() const { return !m_latest.isEmpty() && isNewer(m_latest, m_current); }
    QString state() const { return m_state; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    QUrl packageUrl() const { return m_packageUrl; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void clearError();

    // 0.10.0 is newer than 0.9.2; a leading v and a -suffix are ignored.
    static bool isNewer(const QString &candidate, const QString &current);

signals:
    void changed();
    // The new package is installed; the app should restart into it.
    void installed();

private:
    void setState(const QString &state);
    void fail(const QString &message);
    void releaseFetched(QNetworkReply *reply);
    void packageFetched(QNetworkReply *reply);

    QString m_current, m_latest, m_state = "idle", m_error;
    QUrl m_api, m_packageUrl, m_pageUrl;
    QString m_packageName, m_downloadDir, m_pkexec, m_pacman, m_arch;
    double m_progress = 0;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QProcess m_pacmanRun;
    QTimer m_timer;
};
