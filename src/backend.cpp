#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <pwd.h>
#include <unistd.h>

#include "filepicker.h"
#include "secretstore.h"

Backend::Backend(const Paths &paths, SecretStore *secrets, FilePicker *picker, QObject *parent)
    : QObject(parent), m_secrets(secrets), m_picker(picker) {
    const QString configDir = !paths.configDir.isEmpty() ? paths.configDir
        : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/openvpn-gui";
    const QString dataDir = !paths.dataDir.isEmpty() ? paths.dataDir
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/openvpn-gui";
    m_settings = std::make_unique<QSettings>(configDir + "/settings.ini", QSettings::IniFormat);
    m_store = std::make_unique<ProfileStore>(configDir + "/profiles.json", dataDir + "/profiles");

    m_runtimeDir = paths.runtimeDir;
    if (m_runtimeDir.isEmpty()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (base.isEmpty())
            base = QDir::tempPath();
        m_runtimeDir = base + "/openvpn-gui";
    }
    m_stateDir = !paths.stateDir.isEmpty() ? paths.stateDir
        : QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/openvpn-gui";
    m_openvpn = paths.openvpn.isEmpty() ? QStandardPaths::findExecutable("openvpn") : paths.openvpn;
    // The real path (/usr/bin/openvpn, not /usr/sbin/openvpn through a link),
    // so the polkit rule the package installs recognises it.
    if (const QString real = QFileInfo(m_openvpn).canonicalFilePath(); !real.isEmpty())
        m_openvpn = real;
    m_pkexec = paths.pkexec.isEmpty() ? QStandardPaths::findExecutable("pkexec") : paths.pkexec;

    m_selectedId = m_settings->value("selected").toString();
    if (!m_store->find(m_selectedId))
        m_selectedId = m_store->all().isEmpty() ? QString() : m_store->all().first().id;

    if (m_picker) {
        connect(m_picker, &FilePicker::picked, this, &Backend::configPicked);
        connect(m_picker, &FilePicker::failed, this, &Backend::setError);
    }

    connect(&m_launch, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exitStatus) {
        const QString stderrText = QString::fromUtf8(m_launch.readAllStandardError()).trimmed();
        if (exitStatus == QProcess::NormalExit && code == 0) {
            // OpenVPN has gone to the background; find its management socket.
            setStatus("connecting");
            m_attachAttempts = 0;
            attach();
            return;
        }
        m_activeId.clear();
        QFile::remove(activeFile());
        setStatus("disconnected");
        if (exitStatus == QProcess::NormalExit && code == 126)
            setError(tr("The password prompt was dismissed, so the tunnel was not started."));
        else if (exitStatus == QProcess::NormalExit && code == 127)
            setError(tr("Not allowed to start OpenVPN. Is a polkit agent running?") +
                     (stderrText.isEmpty() ? QString() : "\n\n" + stderrText));
        else {
            const QString details = logTail();
            setError(tr("OpenVPN could not start.") + "\n\n" + (details.isEmpty() ? stderrText : details));
        }
    });
    connect(&m_launch, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart)
            return;
        m_activeId.clear();
        setStatus("disconnected");
        setError(tr("pkexec could not be run: %1").arg(m_launch.errorString()));
    });

    m_stallTimer.setSingleShot(true);
    connect(&m_stallTimer, &QTimer::timeout, this, [this] { setStalled(true); });

    m_attachRetry.setSingleShot(true);
    m_attachRetry.setInterval(200);
    connect(&m_attachRetry, &QTimer::timeout, this, &Backend::attach);

    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_buffer.clear();
        if (m_activeId.isEmpty()) {
            // A tunnel from an earlier session: the launcher noted its profile.
            QFile active(activeFile());
            if (active.open(QIODevice::ReadOnly))
                m_activeId = QString::fromUtf8(active.readAll()).trimmed();
        }
        if (m_status == "disconnected")
            setStatus("connecting");
        // Live state, traffic every two seconds and the log (with its history,
        // since root's log file is not ours to read), then let OpenVPN go on.
        // The hold only gates the first start: with it left on, a restart
        // after a dropped link would wait for a window that may be closed.
        m_log.clear();
        // The history that comes first is already in the kept log when the
        // app attaches again to a running tunnel.
        const QFileInfo kept(keptLogPath());
        m_keptLogSince = kept.exists() ? kept.lastModified().toSecsSinceEpoch() : 0;
        send("state on");
        send("bytecount 2");
        send("log on all");
        send("state");
        send("hold release");
        send("hold off");
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
        m_buffer += m_socket.readAll();
        qsizetype end;
        while ((end = m_buffer.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(m_buffer.left(end)).trimmed();
            m_buffer.remove(0, end + 1);
            if (!line.isEmpty())
                handleLine(line);
        }
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError socketError) {
        if (socketError == QLocalSocket::PeerClosedError)
            return; // disconnected() handles a tunnel that went away.
        if (m_status != "connecting")
            return; // Nothing running at startup is the normal case.
        // Right after launch the daemon may not have opened the socket yet.
        if (++m_attachAttempts < 50) {
            m_attachRetry.start();
            return;
        }
        m_activeId.clear();
        setStatus("disconnected");
        const QString details = logTail();
        setError(tr("OpenVPN started but its control socket never appeared.") +
                 (details.isEmpty() ? QString() : "\n\n" + details));
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        const bool expected = m_stopRequested;
        const bool wasUp = m_status == "connected" || m_status == "connecting";
        resetSession();
        QFile::remove(activeFile());
        setStatus("disconnected");
        if (!expected && wasUp) {
            const QString details = logTail();
            setError(tr("The tunnel closed unexpectedly.") + (details.isEmpty() ? QString() : "\n\n" + details));
        }
    });

    // A tunnel started before this window opened is still ours to show.
    if (QFileInfo::exists(socketPath()))
        attach();
}

Backend::~Backend() {
    m_socket.disconnect(this);
    m_socket.abort();
}

QVariantList Backend::profiles() const {
    QVariantList list;
    for (const auto &profile : m_store->all())
        list.append(QVariantMap{{"id", profile.id},
                                {"name", profile.name},
                                {"host", profile.host},
                                {"username", profile.username}});
    return list;
}

QVariantMap Backend::profile(const QString &id) const {
    const Profile *found = m_store->find(id);
    if (!found)
        return {};
    return {{"id", found->id},
            {"name", found->name},
            {"host", found->host},
            {"username", found->username},
            {"config", found->config}};
}

bool Backend::hasPassword(const QString &id) const {
    return m_secrets && !id.isEmpty() && !m_secrets->lookup(id).isEmpty();
}

void Backend::select(const QString &id) {
    if (id == m_selectedId || (!id.isEmpty() && !m_store->find(id)))
        return;
    m_selectedId = id;
    m_settings->setValue("selected", id);
    emit selectedChanged();
}

QString Backend::activeName() const {
    const Profile *found = m_store->find(m_activeId);
    return found ? found->name : QString();
}

QString Backend::saveProfile(const QString &id, const QString &name, const QString &configPath,
                             const QString &username, const QString &password, bool forgetPassword) {
    Profile profile;
    profile.id = id;
    profile.name = name;
    profile.username = username;
    QString problem;
    const QString saved = m_store->save(profile, configPath, &problem);
    if (saved.isEmpty())
        return problem;

    if (m_secrets) {
        if (forgetPassword || username.trimmed().isEmpty())
            m_secrets->remove(saved);
        else if (!password.isEmpty() &&
                 !m_secrets->store(saved, tr("OpenVPN password for %1").arg(name.trimmed()), password, &problem)) {
            emit profilesChanged();
            return tr("The profile was saved, but the password could not be stored in the keyring: %1").arg(problem);
        }
    }
    emit profilesChanged();
    if (m_activeId == saved)
        emit statusChanged(); // the name on screen may have changed
    select(saved);
    return {};
}

QString Backend::deleteProfile(const QString &id) {
    QString problem;
    if (id == m_activeId && isRunning())
        problem = tr("Disconnect before deleting this profile.");
    else
        m_store->remove(id, &problem);
    if (!problem.isEmpty()) {
        setError(problem);
        return problem;
    }
    if (m_secrets)
        m_secrets->remove(id);
    emit profilesChanged();
    if (m_selectedId == id) {
        m_selectedId.clear();
        select(m_store->all().isEmpty() ? QString() : m_store->all().first().id);
        emit selectedChanged();
    }
    return {};
}

void Backend::toggle() {
    if (m_status == "disconnected")
        connectProfile(m_selectedId);
    else if (m_status == "connected" || m_status == "connecting")
        disconnectVpn();
}

void Backend::connectProfile(const QString &id) {
    if (m_status != "disconnected")
        return;
    clearError();

    const Profile *profile = m_store->find(id);
    if (!profile) {
        setError(m_store->all().isEmpty() ? tr("Add a profile first.") : tr("Choose a profile first."));
        return;
    }
    select(id);
    if (!QFileInfo(profile->config).isReadable()) {
        setError(tr("The profile's copy of its .ovpn file is missing: %1. Edit the profile and choose the file again.")
                     .arg(profile->config));
        return;
    }
    if (m_openvpn.isEmpty() || !QFileInfo(m_openvpn).isExecutable()) {
        setError(tr("OpenVPN is not installed. Install it with: omarchy-pkg-add openvpn"));
        return;
    }
    if (m_pkexec.isEmpty() || !QFileInfo(m_pkexec).isExecutable()) {
        setError(tr("pkexec is not installed, so OpenVPN cannot be started as root."));
        return;
    }

    QDir().mkpath(m_runtimeDir);
    QFile::setPermissions(m_runtimeDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    // Leftovers from a tunnel that died are in our own directory, so they can
    // go even though root created them.
    QFile::remove(socketPath());
    QFile::remove(logPath());
    QSaveFile active(activeFile());
    if (active.open(QIODevice::WriteOnly)) {
        active.write(profile->id.toUtf8());
        active.commit();
    }

    const passwd *account = getpwuid(getuid());
    const QString user = account ? QString::fromLocal8Bit(account->pw_name) : qEnvironmentVariable("USER");
    // Files the profile names (keys, certificates) resolve beside the
    // original when it is still there.
    const QString workDir = QFileInfo(profile->sourceDir).isDir() ? profile->sourceDir
                                                                 : QFileInfo(profile->config).absolutePath();

    m_activeId = profile->id;
    m_stopRequested = false;
    m_savedCredentialsTried = false;
    m_attachAttempts = 0;
    setStatus("authorizing");
    // Only this user may use the management socket, and passwords the profile
    // asks for come to this app instead of a terminal.
    m_launch.start(m_pkexec, {m_openvpn,
                              "--cd", workDir,
                              "--config", profile->config,
                              "--daemon", "openvpn-gui",
                              "--log", logPath(),
                              "--management", socketPath(), "unix",
                              "--management-client-user", user,
                              "--management-hold",
                              "--management-query-passwords",
                              "--auth-retry", "interact"});
}

void Backend::disconnectVpn() {
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return;
    m_stopRequested = true;
    if (!m_credentialsKind.isEmpty()) {
        m_credentialsKind.clear();
        emit credentialsChanged();
    }
    setStatus("disconnecting");
    send("signal SIGTERM");
}

void Backend::chooseConfig() {
    if (m_picker)
        m_picker->openConfig(tr("Choose an OpenVPN profile"));
}

void Backend::askCredentials(const QString &realm) {
    const Profile *profile = m_store->find(m_activeId);
    // Answer from the saved profile once; if the server turns that down, ask.
    if (realm == "Auth" && profile && !m_savedCredentialsTried && !profile->username.isEmpty() && m_secrets) {
        const QString password = m_secrets->lookup(profile->id);
        if (!password.isEmpty()) {
            m_savedCredentialsTried = true;
            send(QString("username %1 %2").arg(quote(realm), quote(profile->username)));
            send(QString("password %1 %2").arg(quote(realm), quote(password)));
            return;
        }
    }
    m_credentialsKind = realm;
    m_credentialsUser = profile ? profile->username : QString();
    emit credentialsChanged();
}

void Backend::submitCredentials(const QString &username, const QString &password, bool remember) {
    if (m_credentialsKind.isEmpty())
        return;
    const QString realm = m_credentialsKind;
    m_credentialsKind.clear();
    emit credentialsChanged();
    if (realm == "Auth")
        send(QString("username %1 %2").arg(quote(realm), quote(username)));
    send(QString("password %1 %2").arg(quote(realm), quote(password)));

    const Profile *profile = m_store->find(m_activeId);
    if (remember && realm == "Auth" && profile) {
        const QString problem = saveProfile(profile->id, profile->name, {}, username, password, false);
        if (!problem.isEmpty())
            setError(problem);
    }
}

void Backend::cancelCredentials() {
    if (m_credentialsKind.isEmpty())
        return;
    // OpenVPN would wait for the answer forever, so stop it instead.
    disconnectVpn();
}

void Backend::clearError() {
    setError({});
}

Backend::State Backend::parseState(const QString &line) {
    // time,STATE,description,local ip,remote ip,remote port,...
    State state;
    const QStringList fields = line.split(',');
    if (fields.size() < 2)
        return state;
    bool ok = false;
    state.time = fields[0].toLongLong(&ok);
    // Real state names are words (WAIT, CONNECTED); short ones are log flags.
    if (!ok || fields[1].size() < 4)
        return state;
    for (const QChar c : fields[1])
        if (!(c.isUpper() || c == '_'))
            return state;
    state.valid = true;
    state.name = fields[1];
    state.localIp = fields.value(3);
    state.remoteIp = fields.value(4);
    return state;
}

QString Backend::quote(const QString &value) {
    QString escaped = value;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    return '"' + escaped + '"';
}

void Backend::handleLine(const QString &line) {
    if (line.startsWith(">STATE:")) {
        applyState(parseState(line.mid(7)));
    } else if (line.startsWith(">BYTECOUNT:")) {
        const QStringList counts = line.mid(11).split(',');
        if (counts.size() == 2) {
            const qint64 in = counts[0].toLongLong();
            if (in > m_bytesIn && m_status == "connected") {
                // Something came from the server: the link is alive.
                setStalled(false);
                m_stallTimer.start(m_stallAfterMs);
            }
            m_bytesIn = in;
            m_bytesOut = counts[1].toLongLong();
            emit trafficChanged();
        }
    } else if (line.startsWith(">PASSWORD:")) {
        const QString request = line.mid(10);
        const qsizetype open = request.indexOf('\''), close = request.indexOf('\'', open + 1);
        const QString realm = open >= 0 && close > open ? request.mid(open + 1, close - open - 1) : QString();
        if (request.startsWith("Verification Failed")) {
            m_savedCredentialsTried = true;
            setError(realm == "Auth" ? tr("The username or password was not accepted.")
                                     : tr("The password was not accepted."));
        } else if (request.startsWith("Need ") && !realm.isEmpty() && !request.contains("SC:") &&
                   !request.contains(" ok")) {
            askCredentials(realm);
        }
    } else if (line.startsWith(">HOLD:")) {
        send("hold release");
    } else if (line.startsWith(">LOG:")) {
        // time,flags,message
        keepLog(line.mid(5));
        const QString message = line.mid(5).section(',', 2);
        // The server's keepalive sets how quickly a silent link counts as stalled.
        static const QRegularExpression ping("^PUSH: Received control message: '.*[',]ping (\\d+)[,']");
        if (const QRegularExpressionMatch m = ping.match(message); m.hasMatch() && m.captured(1).toInt() > 0)
            m_stallAfterMs = qMax(10, 2 * m.captured(1).toInt()) * 1000;
        m_log.append(message);
        if (m_log.size() > 40)
            m_log.removeFirst();
    } else if (line.startsWith(">FATAL:")) {
        setError(line.mid(7));
    } else if (!line.startsWith('>')) {
        // The reply to the plain "state" command looks like a >STATE: line.
        const State state = parseState(line);
        if (state.valid)
            applyState(state);
    }
}

void Backend::applyState(const State &state) {
    if (!state.valid)
        return;
    m_step = state.name;
    if (state.name == "CONNECTED") {
        m_localIp = state.localIp;
        m_remoteIp = state.remoteIp;
        m_connectedSince = state.time;
        clearError();
        setStalled(false);
        m_stallTimer.start(m_stallAfterMs);
        setStatus("connected");
    } else if (state.name == "EXITING") {
        m_stallTimer.stop();
        setStalled(false);
        setStatus("disconnecting");
    } else {
        m_stallTimer.stop();
        setStalled(false);
        m_connectedSince = 0;
        setStatus("connecting");
    }
    emit statusChanged();
}

void Backend::send(const QString &command) {
    if (m_socket.state() == QLocalSocket::ConnectedState)
        m_socket.write(command.toUtf8() + '\n');
}

void Backend::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

void Backend::setStalled(bool stalled) {
    if (stalled == m_stalled)
        return;
    m_stalled = stalled;
    emit statusChanged();
}

void Backend::keepLog(const QString &entry) {
    // time,flags,message
    const qint64 time = entry.section(',', 0, 0).toLongLong();
    QString message = entry.section(',', 2);
    if (time < m_keptLogSince || message.isEmpty())
        return;
    if (message.startsWith("MANAGEMENT: CMD 'password"))
        message = "MANAGEMENT: CMD 'password [hidden]'";
    QDir().mkpath(m_stateDir);
    QFile::setPermissions(m_stateDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QString path = keptLogPath();
    if (QFileInfo(path).size() > 1024 * 1024) {
        QFile::remove(path + ".1");
        QFile::rename(path, path + ".1");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const QString stamp = QDateTime::fromSecsSinceEpoch(time).toString("yyyy-MM-dd HH:mm:ss");
    file.write((stamp + ' ' + message + '\n').toUtf8());
}

void Backend::resetSession() {
    m_attachRetry.stop();
    m_stallTimer.stop();
    m_stalled = false;
    m_stopRequested = false;
    m_activeId.clear();
    m_step.clear();
    m_localIp.clear();
    m_remoteIp.clear();
    m_connectedSince = 0;
    m_bytesIn = m_bytesOut = 0;
    m_credentialsKind.clear();
    emit credentialsChanged();
    emit trafficChanged();
}

void Backend::attach() {
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    m_socket.connectToServer(socketPath());
}

QString Backend::logTail(int lines) const {
    // What OpenVPN told the management socket, else its log file when that is
    // readable (it is root's, so usually only the socket has it).
    QStringList all = m_log;
    if (all.isEmpty()) {
        QFile log(logPath());
        if (log.open(QIODevice::ReadOnly))
            all = QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts);
    }
    // Lines that name the problem matter more than routine ones.
    QStringList useful;
    for (const QString &line : all)
        if (!line.startsWith("MANAGEMENT:") && !line.trimmed().isEmpty())
            useful.append(line.trimmed());
    return useful.mid(qMax(0, useful.size() - lines)).join('\n');
}
