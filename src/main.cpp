// openvpn-gui — saved OpenVPN profiles, switched on and off from the window or
// the Omarchy bar.

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>

#include "backend.h"
#include "portalfilepicker.h"
#include "secretstore.h"
#include "shell.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // QApplication rather than QGuiApplication: the icon in the bar
    // (QSystemTrayIcon and its menu) lives in Qt Widgets.
    QApplication app(argc, argv);
    app.setApplicationName("openvpn-gui");
    app.setApplicationVersion("0.2.0");
    app.setQuitOnLastWindowClosed(false);

    // Matches the window to openvpn-gui.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("openvpn-gui");
    app.setWindowIcon(QIcon::fromTheme("openvpn-gui"));

    QQuickStyle::setStyle("Material");

    // A profile opened from the file manager or the command line is offered
    // for import.
    QString importPath;
    if (app.arguments().size() > 1) {
        importPath = app.arguments().at(1);
        if (importPath.startsWith("file://"))
            importPath = QUrl(importPath).toLocalFile();
        importPath = QFileInfo(importPath).absoluteFilePath();
    }

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker picker;
    KeyringStore keyring;
    Backend backend({}, &keyring, &picker);
    Shell shell(&backend, &theme);

    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!shell.claimInstance((runtime.isEmpty() ? QDir::tempPath() : runtime) + "/openvpn-gui", importPath))
        return 0; // The running instance shows its window instead.

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("shell", &shell);
    engine.rootContext()->setContextProperty("initialImport", importPath);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    shell.setWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));

    return app.exec();
}
