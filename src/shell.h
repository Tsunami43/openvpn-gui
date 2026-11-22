#pragma once

#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>

class Backend;
class Theme;
class QMenu;
class QSystemTrayIcon;
class QWindow;

// Everything around the window: the icon in the Omarchy bar that keeps the
// app running after its window closes, the single running instance a second
// launch hands over to, and quitting for real.
class Shell : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)

public:
    Shell(Backend *backend, Theme *theme, QObject *parent = nullptr);
    ~Shell() override;

    // Returns false when another instance is already running; it has been
    // asked to show its window (and import the profile, if one was given).
    bool claimInstance(const QString &runtimeDir, const QString &importPath);

    void setWindow(QWindow *window);
    bool trayAvailable() const { return m_trayAvailable; }

    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void hideWindow();
    // Switches the tunnel off first, so nothing keeps running unseen.
    Q_INVOKABLE void quit();

signals:
    void importRequested(const QString &path);

private:
    void rebuildMenu();
    void refreshIcon();

    Backend *m_backend;
    Theme *m_theme;
    QPointer<QWindow> m_window;
    bool m_trayAvailable = false;
    bool m_quitting = false;
    std::unique_ptr<QMenu> m_menu;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    QLocalServer m_server;
    QTimer m_quitTimeout;
    QTimer m_menuRebuild;
};
