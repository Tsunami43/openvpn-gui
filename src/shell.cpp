#include "shell.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSystemTrayIcon>
#include <QWindow>

#include "backend.h"
#include "theme.h"

namespace {

// The bar icon: the tunnel mouth from the app icon, drawn in code so it can
// take the theme accent. Hollow when off, an accent outline while connecting,
// and filled with the light at its end when connected.
QIcon tunnelIcon(const QString &status, const QColor &accent) {
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 64.0, size / 64.0);

        QPainterPath arch;
        arch.moveTo(10, 56);
        arch.lineTo(10, 32);
        arch.arcTo(10, 10, 44, 44, 180, -180);
        arch.lineTo(54, 56);

        const bool up = status == "connected";
        const bool busy = !up && status != "disconnected";
        const QColor ink = up || busy ? accent : QColor("#d8d8de");
        if (up) {
            QPainterPath filled = arch;
            filled.closeSubpath();
            painter.fillPath(filled, accent);
            painter.setBrush(QColor("#0e0e10"));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(QRectF(25, 30, 14, 26), 7, 7);
        } else {
            painter.setPen(QPen(ink, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(arch);
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawEllipse(QPointF(32, 40), busy ? 6 : 4, busy ? 6 : 4);
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

Shell::Shell(Backend *backend, Theme *theme, QObject *parent)
    : QObject(parent), m_backend(backend), m_theme(theme) {
    m_quitTimeout.setSingleShot(true);
    m_quitTimeout.setInterval(5000);
    connect(&m_quitTimeout, &QTimer::timeout, qApp, &QCoreApplication::quit);
    connect(m_backend, &Backend::statusChanged, this, [this] {
        if (m_quitting && !m_backend->isRunning())
            qApp->quit();
    });

    m_trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
}

Shell::~Shell() = default;

bool Shell::claimInstance(const QString &runtimeDir, const QString &importPath) {
    QDir().mkpath(runtimeDir);
    const QString name = runtimeDir + "/instance.sock";
    QLocalSocket other;
    other.connectToServer(name);
    if (other.waitForConnected(500)) {
        other.write(("show\n" + importPath + "\n").toUtf8());
        other.flush();
        other.waitForBytesWritten(500);
        return false;
    }
    QLocalServer::removeServer(name);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.listen(name);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server.nextPendingConnection()) {
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                const QStringList lines = QString::fromUtf8(client->readAll()).split('\n');
                showWindow();
                if (lines.value(1).trimmed().size())
                    emit importRequested(lines.value(1).trimmed());
            });
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });
    return true;
}

void Shell::setWindow(QWindow *window) {
    m_window = window;
    // The bar icon appears only now, once this is known to be the one
    // running instance.
    if (!m_trayAvailable || m_tray)
        return;

    m_menu = std::make_unique<QMenu>();
    m_tray = std::make_unique<QSystemTrayIcon>();
    m_tray->setContextMenu(m_menu.get());
    connect(m_tray.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
            return;
        if (m_window && m_window->isVisible() && m_window->isActive())
            hideWindow();
        else
            showWindow();
    });
    // The menu is rebuilt on the next turn of the event loop, never while one
    // of its own actions is still being handled.
    m_menuRebuild.setSingleShot(true);
    m_menuRebuild.setInterval(0);
    connect(&m_menuRebuild, &QTimer::timeout, this, &Shell::rebuildMenu);
    connect(m_backend, &Backend::statusChanged, this, [this] { refreshIcon(); m_menuRebuild.start(); });
    connect(m_backend, &Backend::profilesChanged, &m_menuRebuild, qOverload<>(&QTimer::start));
    connect(m_theme, &Theme::changed, this, &Shell::refreshIcon);
    refreshIcon();
    rebuildMenu();
    m_tray->show();
}

void Shell::showWindow() {
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

void Shell::hideWindow() {
    if (m_window)
        m_window->hide();
}

void Shell::quit() {
    if (!m_backend->isRunning() || m_backend->status() == "authorizing") {
        qApp->quit();
        return;
    }
    m_quitting = true;
    hideWindow();
    m_backend->disconnectVpn();
    m_quitTimeout.start();
}

void Shell::refreshIcon() {
    if (!m_tray)
        return;
    const QString status = m_backend->status();
    m_tray->setIcon(tunnelIcon(status, QColor(m_theme->accent())));
    const QString name = m_backend->activeName();
    if (status == "connected" && m_backend->stalled())
        m_tray->setToolTip(tr("OpenVPN: no connection to the server (%1)").arg(name));
    else if (status == "connected")
        m_tray->setToolTip(tr("OpenVPN: connected to %1").arg(name));
    else if (status == "disconnected")
        m_tray->setToolTip(tr("OpenVPN: disconnected"));
    else
        m_tray->setToolTip(tr("OpenVPN: connecting to %1").arg(name));
}

void Shell::rebuildMenu() {
    if (!m_menu)
        return;
    m_menu->clear();

    m_menu->addAction(tr("Open OpenVPN GUI"), this, &Shell::showWindow);
    m_menu->addSeparator();

    const bool running = m_backend->isRunning();
    const QString activeId = m_backend->activeId();
    for (const QVariant &value : m_backend->profiles()) {
        const QVariantMap profile = value.toMap();
        const QString id = profile.value("id").toString();
        QAction *action = m_menu->addAction(profile.value("name").toString());
        action->setCheckable(true);
        action->setChecked(running && id == activeId);
        action->setEnabled(!running || id == activeId);
        connect(action, &QAction::triggered, this, [this, id](bool) {
            if (m_backend->isRunning())
                m_backend->disconnectVpn();
            else
                m_backend->connectProfile(id);
        });
    }
    if (m_backend->profiles().isEmpty())
        m_menu->addAction(tr("Add a profile…"), this, &Shell::showWindow);

    if (running) {
        m_menu->addSeparator();
        m_menu->addAction(tr("Disconnect"), m_backend, &Backend::disconnectVpn);
    }
    m_menu->addSeparator();
    m_menu->addAction(running ? tr("Disconnect and quit") : tr("Quit"), this, &Shell::quit);
}
