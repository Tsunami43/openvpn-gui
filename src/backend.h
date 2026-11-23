#pragma once

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QVariantList>

#include <array>
#include <memory>

#include "profiles.h"

class FilePicker;
class SecretStore;

// Runs OpenVPN for a saved profile and reports how the tunnel is doing.
//
// OpenVPN needs root, so it is started once through pkexec (Omarchy's own
// password dialog) as a daemon with a management socket in the user's runtime
// directory. Everything after that, the state, traffic, the profile's
// username and password, and switching off, goes over that socket and needs
// no further root password. Reopening the app attaches to a running tunnel.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(QString activeId READ activeId NOTIFY statusChanged)
    Q_PROPERTY(QString activeName READ activeName NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString step READ step NOTIFY statusChanged)
    Q_PROPERTY(QString localIp READ localIp NOTIFY statusChanged)
    Q_PROPERTY(QString remoteIp READ remoteIp NOTIFY statusChanged)
    Q_PROPERTY(qint64 connectedSince READ connectedSince NOTIFY statusChanged)
    // Connected, but nothing has come from the server for a while.
    Q_PROPERTY(bool stalled READ stalled NOTIFY statusChanged)
    Q_PROPERTY(double bytesIn READ bytesIn NOTIFY trafficChanged)
    Q_PROPERTY(double bytesOut READ bytesOut NOTIFY trafficChanged)
    // Bytes per second, from the two newest traffic reports.
    Q_PROPERTY(double rateIn READ rateIn NOTIFY trafficChanged)
    Q_PROPERTY(double rateOut READ rateOut NOTIFY trafficChanged)
    Q_PROPERTY(int historySize READ historySize CONSTANT)
    Q_PROPERTY(int sampleMs READ sampleMs CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString credentialsKind READ credentialsKind NOTIFY credentialsChanged)
    Q_PROPERTY(QString credentialsUser READ credentialsUser NOTIFY credentialsChanged)

public:
    struct Paths {
        QString configDir;  // empty: ~/.config/openvpn-gui
        QString dataDir;    // empty: ~/.local/share/openvpn-gui
        QString runtimeDir; // empty: $XDG_RUNTIME_DIR/openvpn-gui
        QString stateDir;   // empty: ~/.local/state/openvpn-gui (the kept log)
        QString openvpn;    // empty: found on PATH
        QString pkexec;     // empty: found on PATH
    };

    Backend(const Paths &paths, SecretStore *secrets, FilePicker *picker = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    QVariantList profiles() const;
    QString selectedId() const { return m_selectedId; }
    void select(const QString &id);
    QString activeId() const { return m_activeId; }
    QString activeName() const;

    // disconnected, authorizing, connecting, connected, disconnecting
    QString status() const { return m_status; }
    // OpenVPN's own state name while connecting (WAIT, AUTH, GET_CONFIG...).
    QString step() const { return m_step; }
    QString localIp() const { return m_localIp; }
    QString remoteIp() const { return m_remoteIp; }
    qint64 connectedSince() const { return m_connectedSince; }
    bool stalled() const { return m_stalled; }
    double bytesIn() const { return double(m_bytesIn); }
    double bytesOut() const { return double(m_bytesOut); }
    double rateIn() const { return m_rateIn; }
    double rateOut() const { return m_rateOut; }
    // The tunnel's speed over the last ten minutes, oldest first, one sample
    // every two seconds: {in: [bytes/s], out: [bytes/s], down: [bool]}, where
    // down marks moments the tunnel was reconnecting or heard nothing from
    // the server. Read when the graph paints, not on every report.
    static constexpr int HistorySize = 300;
    static constexpr int SampleMs = 2000;
    int historySize() const { return HistorySize; }
    int sampleMs() const { return SampleMs; }
    Q_INVOKABLE QVariantMap trafficHistory() const;
    QString error() const { return m_error; }
    // Empty, "Auth" (username and password) or another realm (password only).
    QString credentialsKind() const { return m_credentialsKind; }
    QString credentialsUser() const { return m_credentialsUser; }

    QString socketPath() const { return m_runtimeDir + "/management.sock"; }
    QString logPath() const { return m_runtimeDir + "/openvpn.log"; }
    // OpenVPN's log as the management socket tells it, kept across tunnels
    // and readable without root.
    QString keptLogPath() const { return m_stateDir + "/openvpn.log"; }
    // How long a connected tunnel may hear nothing from the server before it
    // counts as stalled: two of the server's pings (ping 10 unless pushed).
    int stallAfterMs() const { return m_stallAfterMs; }
    QString activeFile() const { return m_runtimeDir + "/active"; }
    bool isRunning() const { return m_status != "disconnected"; }

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void connectProfile(const QString &id);
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE QVariantMap profile(const QString &id) const;
    Q_INVOKABLE bool hasPassword(const QString &id) const;
    // Returns an empty string on success, else what went wrong. An empty
    // password keeps the saved one unless forget is set.
    Q_INVOKABLE QString saveProfile(const QString &id, const QString &name, const QString &configPath,
                                    const QString &username, const QString &password, bool forgetPassword);
    Q_INVOKABLE QString deleteProfile(const QString &id);
    Q_INVOKABLE void chooseConfig();
    Q_INVOKABLE void submitCredentials(const QString &username, const QString &password, bool remember);
    Q_INVOKABLE void cancelCredentials();
    Q_INVOKABLE void clearError();

    // Management protocol helpers, public so tests can check them.
    struct State {
        bool valid = false;
        qint64 time = 0;
        QString name, localIp, remoteIp;
    };
    static State parseState(const QString &line);
    static QString quote(const QString &value);

signals:
    void profilesChanged();
    void selectedChanged();
    void statusChanged();
    void trafficChanged();
    void errorChanged();
    void credentialsChanged();
    void configPicked(const QString &path);

private:
    void attach();
    void handleLine(const QString &line);
    void applyState(const State &state);
    void send(const QString &command);
    void setStatus(const QString &status);
    void setError(const QString &message);
    void askCredentials(const QString &realm);
    void resetSession();
    QString logTail(int lines = 6) const;
    void keepLog(const QString &line);
    void setStalled(bool stalled);
    void addSample(float in, float out, bool down);

    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<ProfileStore> m_store;
    SecretStore *m_secrets = nullptr;
    QString m_runtimeDir, m_stateDir, m_openvpn, m_pkexec;
    FilePicker *m_picker = nullptr;

    QString m_selectedId, m_activeId;
    QString m_status = "disconnected";
    QString m_step, m_localIp, m_remoteIp, m_error, m_credentialsKind, m_credentialsUser;
    qint64 m_connectedSince = 0, m_bytesIn = 0, m_bytesOut = 0;
    bool m_stopRequested = false;
    bool m_savedCredentialsTried = false;
    bool m_stalled = false;
    int m_stallAfterMs = 20000;
    QTimer m_stallTimer;
    qint64 m_keptLogSince = 0; // log lines older than this are already kept

    struct Sample {
        float in = 0, out = 0;
        bool down = false;
    };
    std::array<Sample, HistorySize> m_history{};
    int m_historyHead = 0, m_historyCount = 0;
    double m_rateIn = 0, m_rateOut = 0;
    bool m_haveCounts = false;   // a first report to measure the next against
    QElapsedTimer m_sinceCounts; // since the last traffic report
    QTimer m_gapTimer;           // no report in time: the graph still moves on

    QProcess m_launch;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QStringList m_log; // the newest lines of OpenVPN's log, from the socket
    QTimer m_attachRetry;
    int m_attachAttempts = 0;
};
