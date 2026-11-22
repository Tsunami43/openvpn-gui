#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "filepicker.h"
#include "profiles.h"
#include "secretstore.h"
#include "theme.h"
#include "translator.h"
#include "updater.h"

class FakePicker : public FilePicker {
public:
    using FilePicker::FilePicker;
    void openConfig(const QString &) override { emit picked(next); }
    QString next;
};

// Stands in for OpenVPN's management interface on a socket in a temp dir.
struct FakeOpenvpn {
    QLocalServer server;
    QLocalSocket *peer = nullptr;
    QByteArray received;

    bool listen(const QString &path) { return server.listen(path); }
    bool accept() {
        for (int i = 0; i < 100 && !peer; ++i) {
            server.waitForNewConnection(20);
            peer = server.nextPendingConnection();
            QCoreApplication::processEvents();
        }
        return peer;
    }
    QByteArray seen() {
        peer->waitForReadyRead(20);
        received += peer->readAll();
        return received;
    }
    void say(const QByteArray &lines) { peer->write(lines); peer->flush(); }
};

class OpenvpnGuiTests : public QObject {
    Q_OBJECT

private slots:
    void parsesStateLines()
    {
        const auto state = Backend::parseState("1700000000,CONNECTED,SUCCESS,10.8.0.6,203.0.113.5,1194,,");
        QVERIFY(state.valid);
        QCOMPARE(state.time, 1700000000);
        QCOMPARE(state.name, QString("CONNECTED"));
        QCOMPARE(state.localIp, QString("10.8.0.6"));
        QCOMPARE(state.remoteIp, QString("203.0.113.5"));
        QVERIFY(Backend::parseState("1700000000,WAIT,,,").valid);
        QVERIFY(!Backend::parseState("SUCCESS: real-time state notification set to ON").valid);
        QVERIFY(!Backend::parseState("END").valid);
    }

    void quotesManagementArguments()
    {
        QCOMPARE(Backend::quote("plain"), QString("\"plain\""));
        QCOMPARE(Backend::quote("a\"b\\c"), QString("\"a\\\"b\\\\c\""));
    }

    void readsTheServerFromAProfile()
    {
        QCOMPARE(ProfileStore::hostOf("client\ndev tun\nremote vpn.example.com 1194 udp\nremote b.example.com\n"),
                 QString("vpn.example.com"));
        QCOMPARE(ProfileStore::hostOf("client\n"), QString());
    }

    void createsAProfileFromAnOvpn()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        Backend backend(paths(dir), &secrets);
        const QString original = dir.filePath("office.ovpn");
        write(original, "client\nremote vpn.example.com 1194\n");

        QCOMPARE(backend.saveProfile({}, "Office", original, "alice", "s3cret", false), QString());
        QCOMPARE(backend.profiles().size(), 1);
        const QVariantMap profile = backend.profiles().first().toMap();
        const QString id = profile.value("id").toString();
        QCOMPARE(profile.value("name").toString(), QString("Office"));
        QCOMPARE(profile.value("host").toString(), QString("vpn.example.com"));
        QCOMPARE(profile.value("username").toString(), QString("alice"));
        QCOMPARE(backend.selectedId(), id);
        QCOMPARE(secrets.lookup(id), QString("s3cret"));
        QVERIFY(backend.hasPassword(id));

        // The profile keeps its own private copy; the original is untouched.
        const QString copy = backend.profile(id).value("config").toString();
        QVERIFY(copy.startsWith(dir.filePath("data")));
        QCOMPARE(QFile::permissions(copy) & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
        QFile::remove(original);
        QVERIFY(QFile::exists(copy));

        // It is all still there after a restart.
        Backend again(paths(dir), &secrets);
        QCOMPARE(again.profiles().size(), 1);
        QCOMPARE(again.selectedId(), id);
    }

    void rejectsBadProfiles()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        Backend backend(paths(dir), &secrets);
        write(dir.filePath("notes.txt"), "hello\n");
        QVERIFY(!backend.saveProfile({}, "", dir.filePath("notes.txt"), {}, {}, false).isEmpty());
        QVERIFY(!backend.saveProfile({}, "Home", {}, {}, {}, false).isEmpty());
        QVERIFY(!backend.saveProfile({}, "Home", dir.filePath("missing.ovpn"), {}, {}, false).isEmpty());
        QVERIFY(!backend.saveProfile({}, "Home", dir.filePath("notes.txt"), {}, {}, false).isEmpty());
        QCOMPARE(backend.profiles().size(), 0);
    }

    void editsKeepOrForgetThePassword()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        Backend backend(paths(dir), &secrets);
        write(dir.filePath("a.ovpn"), "client\nremote a.example.com\n");
        backend.saveProfile({}, "A", dir.filePath("a.ovpn"), "bob", "pw", false);
        const QString id = backend.selectedId();

        // An empty password and no new file keep both.
        QCOMPARE(backend.saveProfile(id, "Renamed", {}, "bob", {}, false), QString());
        QCOMPARE(backend.profile(id).value("name").toString(), QString("Renamed"));
        QCOMPARE(backend.profile(id).value("host").toString(), QString("a.example.com"));
        QCOMPARE(secrets.lookup(id), QString("pw"));

        // A new file replaces the copy.
        write(dir.filePath("b.ovpn"), "client\nremote b.example.com\n");
        QCOMPARE(backend.saveProfile(id, "Renamed", dir.filePath("b.ovpn"), "bob", {}, false), QString());
        QCOMPARE(backend.profile(id).value("host").toString(), QString("b.example.com"));

        QCOMPARE(backend.saveProfile(id, "Renamed", {}, "bob", {}, true), QString());
        QVERIFY(!backend.hasPassword(id));
    }

    void deletesAProfileAndItsSecrets()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        Backend backend(paths(dir), &secrets);
        write(dir.filePath("a.ovpn"), "client\n");
        backend.saveProfile({}, "A", dir.filePath("a.ovpn"), "bob", "pw", false);
        const QString id = backend.selectedId();
        const QString copy = backend.profile(id).value("config").toString();
        QCOMPARE(backend.deleteProfile(id), QString());
        QCOMPARE(backend.profiles().size(), 0);
        QVERIFY(!QFile::exists(copy));
        QVERIFY(QFile::exists(dir.filePath("a.ovpn")));
        QCOMPARE(secrets.lookup(id), QString());
        QCOMPARE(backend.selectedId(), QString());
    }

    void pickerHandsBackThePath()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        FakePicker picker;
        picker.next = "/tmp/picked.ovpn";
        Backend backend(paths(dir), &secrets, &picker);
        QSignalSpy spy(&backend, &Backend::configPicked);
        backend.chooseConfig();
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().toString(), QString("/tmp/picked.ovpn"));
    }

    void saysWhatIsMissingInsteadOfStarting()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        Backend backend(paths(dir), &secrets);
        backend.toggle();
        QCOMPARE(backend.status(), QString("disconnected"));
        QVERIFY(!backend.error().isEmpty());

        write(dir.filePath("a.ovpn"), "client\n");
        auto p = paths(dir);
        p.openvpn = dir.filePath("no-openvpn");
        Backend noOpenvpn(p, &secrets);
        noOpenvpn.saveProfile({}, "A", dir.filePath("a.ovpn"), {}, {}, false);
        noOpenvpn.toggle();
        QCOMPARE(noOpenvpn.status(), QString("disconnected"));
        QVERIFY(noOpenvpn.error().contains("openvpn"));
    }

    void launchesThroughPkexecWithTheProfile()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        write(dir.filePath("work.ovpn"), "client\n");
        // A fake pkexec records its arguments and fails like a dismissed prompt.
        const QString pkexec = dir.filePath("pkexec");
        write(pkexec, "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + dir.filePath("args").toUtf8() + "'\nexit 126\n");
        QFile::setPermissions(pkexec, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        auto p = paths(dir);
        p.pkexec = pkexec;
        p.openvpn = pkexec; // any executable stands in for openvpn here
        Backend backend(p, &secrets);
        backend.saveProfile({}, "Work", dir.filePath("work.ovpn"), {}, {}, false);
        const QString id = backend.selectedId();
        backend.connectProfile(id);
        QCOMPARE(backend.status(), QString("authorizing"));
        QCOMPARE(backend.activeId(), id);
        QTRY_COMPARE(backend.status(), QString("disconnected"));
        QVERIFY(!backend.error().isEmpty());
        QCOMPARE(backend.activeId(), QString());

        QFile args(dir.filePath("args"));
        QVERIFY(args.open(QIODevice::ReadOnly));
        const QStringList list = QString::fromUtf8(args.readAll()).split('\n');
        QCOMPARE(list.value(1), QString("--cd"));
        QCOMPARE(list.value(2), dir.path()); // beside the original
        QVERIFY(list.contains(backend.profile(id).value("config").toString()));
        QVERIFY(list.contains(backend.socketPath()));
        QVERIFY(list.contains("--management-query-passwords"));
    }

    void answersWithTheSavedLoginThenAsks()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        write(dir.filePath("a.ovpn"), "client\n");
        QString id;
        {
            Backend setup(paths(dir), &secrets);
            setup.saveProfile({}, "Office", dir.filePath("a.ovpn"), "alice", "pw\"1", false);
            id = setup.selectedId();
        }
        // A tunnel already running for that profile, as after a restart.
        QDir().mkpath(dir.filePath("run"));
        write(dir.filePath("run/active"), id.toUtf8());
        FakeOpenvpn openvpn;
        QVERIFY(openvpn.listen(dir.filePath("run/management.sock")));

        Backend backend(paths(dir), &secrets);
        QVERIFY(openvpn.accept());
        QTRY_VERIFY(openvpn.seen().contains("hold release"));
        QTRY_COMPARE(backend.activeId(), id);
        QCOMPARE(backend.activeName(), QString("Office"));

        openvpn.say(">STATE:1700000000,AUTH,,,,\n>PASSWORD:Need 'Auth' username/password\n");
        QTRY_VERIFY(openvpn.seen().contains("password \"Auth\" \"pw\\\"1\""));
        QVERIFY(openvpn.seen().contains("username \"Auth\" \"alice\""));
        QCOMPARE(backend.credentialsKind(), QString());

        // Turned down: the next request goes to the user, with the name filled in.
        openvpn.say(">PASSWORD:Verification Failed: 'Auth'\n>PASSWORD:Need 'Auth' username/password\n");
        QTRY_COMPARE(backend.credentialsKind(), QString("Auth"));
        QCOMPARE(backend.credentialsUser(), QString("alice"));
        QVERIFY(!backend.error().isEmpty());
        backend.submitCredentials("alice", "new", true);
        QTRY_VERIFY(openvpn.seen().contains("password \"Auth\" \"new\""));
        QCOMPARE(secrets.lookup(id), QString("new"));

        openvpn.say(">STATE:1700000000,CONNECTED,SUCCESS,10.8.0.6,203.0.113.5,1194,,\n>BYTECOUNT:2048,512\n");
        QTRY_COMPARE(backend.status(), QString("connected"));
        QCOMPARE(backend.localIp(), QString("10.8.0.6"));
        QTRY_COMPARE(backend.bytesIn(), 2048.0);
        QCOMPARE(backend.bytesOut(), 512.0);
        QVERIFY(backend.error().isEmpty());

        // A running profile cannot be deleted.
        QVERIFY(!backend.deleteProfile(id).isEmpty());

        backend.toggle();
        QCOMPARE(backend.status(), QString("disconnecting"));
        QTRY_VERIFY(openvpn.seen().contains("signal SIGTERM"));
        openvpn.peer->disconnectFromServer();
        QTRY_COMPARE(backend.status(), QString("disconnected"));
        QCOMPARE(backend.activeId(), QString());
        QVERIFY(!QFile::exists(dir.filePath("run/active")));
    }

    void reportsATunnelThatDies()
    {
        QTemporaryDir dir;
        MemoryStore secrets;
        QDir().mkpath(dir.filePath("run"));
        FakeOpenvpn openvpn;
        QVERIFY(openvpn.listen(dir.filePath("run/management.sock")));
        Backend backend(paths(dir), &secrets);
        QVERIFY(openvpn.accept());
        // Restarts after a dropped link must not wait for this window.
        QTRY_VERIFY(openvpn.seen().contains("hold off"));
        QVERIFY(openvpn.seen().contains("log on all"));
        openvpn.say(">STATE:1700000000,CONNECTED,SUCCESS,10.8.0.6,203.0.113.5,1194,,\n");
        QTRY_COMPARE(backend.status(), QString("connected"));
        // Log lines with one-letter flags are not mistaken for states.
        openvpn.say(">LOG:1700000001,I,[server] Inactivity timeout (--ping-restart), restarting\n"
                    ">LOG:1700000002,W,Exiting due to fatal error\n");
        QTest::qWait(50);
        QCOMPARE(backend.status(), QString("connected"));
        openvpn.peer->disconnectFromServer();
        QTRY_COMPARE(backend.status(), QString("disconnected"));
        // The window says why, in OpenVPN's words.
        QVERIFY(backend.error().contains("Exiting due to fatal error"));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Connected").isNull());
    }

private:
    static Backend::Paths paths(const QTemporaryDir &dir)
    {
        Backend::Paths p;
        p.configDir = dir.filePath("config");
        p.dataDir = dir.filePath("data");
        p.runtimeDir = dir.filePath("run");
        return p;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

private slots:
    void comparesVersions() {
        QVERIFY(Updater::isNewer("0.3.0", "0.2.0"));
        QVERIFY(Updater::isNewer("v0.10.0", "0.9.2"));
        QVERIFY(Updater::isNewer("1.0", "0.9.9"));
        QVERIFY(!Updater::isNewer("0.2.0", "0.2.0"));
        QVERIFY(!Updater::isNewer("0.2", "0.2.0"));
        QVERIFY(!Updater::isNewer("v0.1.9", "0.2.0"));
    }

    void saysWhenANewerReleaseIsOut() {
        QTemporaryDir dir;
        auto release = [&](const QJsonObject &json) {
            QFile f(dir.filePath("latest.json"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QJsonDocument(json).toJson());
        };
        Updater::Options o;
        o.releaseApi = QUrl::fromLocalFile(dir.filePath("latest.json"));
        o.checkEveryMs = 0;
        Updater updater("0.2.0", o);
        QSignalSpy changed(&updater, &Updater::changed);

        release({{"tag_name", "v0.2.0"}, {"html_url", "https://example.invalid/0.2.0"}});
        updater.check();
        QTRY_COMPARE(changed.count(), 1);
        QVERIFY(!updater.available());

        // The pacman repository's rolling release is not a version.
        release({{"tag_name", "repo-x86_64"}, {"prerelease", true}});
        updater.check();
        QTest::qWait(50);
        QCOMPARE(changed.count(), 1);

        release({{"tag_name", "v0.4.0"}, {"html_url", "https://example.invalid/0.4.0"}});
        updater.check();
        QTRY_VERIFY(updater.available());
        QCOMPARE(updater.latestVersion(), QString("0.4.0"));
        QCOMPARE(updater.releasePage(), QUrl("https://example.invalid/0.4.0"));
    }
};

QTEST_MAIN(OpenvpnGuiTests)
#include "openvpn-gui_tests.moc"
