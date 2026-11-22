#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QVariantList>

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
    Q_PROPERTY(double bytesIn READ bytesIn NOTIFY trafficChanged)
    Q_PROPERTY(double bytesOut READ bytesOut NOTIFY trafficChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString credentialsKind READ credentialsKind NOTIFY credentialsChanged)
    Q_PROPERTY(QString credentialsUser READ credentialsUser NOTIFY credentialsChanged)

public:
    struct Paths {
        QString configDir;  // empty: ~/.config/openvpn-gui
        QString dataDir;    // empty: ~/.local/share/openvpn-gui
        QString runtimeDir; // empty: $XDG_RUNTIME_DIR/openvpn-gui
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
    double bytesIn() const { return double(m_bytesIn); }
    double bytesOut() const { return double(m_bytesOut); }
    QString error() const { return m_error; }
    // Empty, "Auth" (username and password) or another realm (password only).
    QString credentialsKind() const { return m_credentialsKind; }
    QString credentialsUser() const { return m_credentialsUser; }

    QString socketPath() const { return m_runtimeDir + "/management.sock"; }
    QString logPath() const { return m_runtimeDir + "/openvpn.log"; }
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

    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<ProfileStore> m_store;
    SecretStore *m_secrets = nullptr;
    QString m_runtimeDir, m_openvpn, m_pkexec;
    FilePicker *m_picker = nullptr;

    QString m_selectedId, m_activeId;
    QString m_status = "disconnected";
    QString m_step, m_localIp, m_remoteIp, m_error, m_credentialsKind, m_credentialsUser;
    qint64 m_connectedSince = 0, m_bytesIn = 0, m_bytesOut = 0;
    bool m_stopRequested = false;
    bool m_savedCredentialsTried = false;

    QProcess m_launch;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QStringList m_log; // the newest lines of OpenVPN's log, from the socket
    QTimer m_attachRetry;
    int m_attachAttempts = 0;
};
