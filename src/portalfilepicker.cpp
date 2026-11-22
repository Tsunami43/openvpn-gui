#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QRandomGenerator>
#include <QUrl>

namespace {

// The portal's filter type is a(sa(us)): a list of named filters, each a list
// of (kind, pattern) pairs where kind 0 is a glob.
struct FilterPattern {
    uint kind = 0;
    QString pattern;
};
struct Filter {
    QString name;
    QList<FilterPattern> patterns;
};
using FilterList = QList<Filter>;

QDBusArgument &operator<<(QDBusArgument &argument, const FilterPattern &value) {
    argument.beginStructure();
    argument << value.kind << value.pattern;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, FilterPattern &value) {
    argument.beginStructure();
    argument >> value.kind >> value.pattern;
    argument.endStructure();
    return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const Filter &value) {
    argument.beginStructure();
    argument << value.name << value.patterns;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, Filter &value) {
    argument.beginStructure();
    argument >> value.name >> value.patterns;
    argument.endStructure();
    return argument;
}

const QString kService = "org.freedesktop.portal.Desktop";
const QString kPath = "/org/freedesktop/portal/desktop";

} // namespace

Q_DECLARE_METATYPE(FilterPattern)
Q_DECLARE_METATYPE(Filter)

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {
    qDBusRegisterMetaType<FilterPattern>();
    qDBusRegisterMetaType<QList<FilterPattern>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<FilterList>();
}

void PortalFilePicker::openConfig(const QString &title) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(tr("The session bus is not available, so no file chooser can be shown."));
        return;
    }

    // Subscribing to the predictable request path before calling avoids
    // missing a response that arrives before the call returns.
    const QString token = QString("tunnel%1").arg(QRandomGenerator::global()->generate());
    const QString sender = bus.baseService().mid(1).replace('.', '_');
    const QString expected = QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    if (!m_requestPath.isEmpty())
        bus.disconnect(kService, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                       SLOT(handleResponse(uint, QVariantMap)));
    m_requestPath = expected;
    bus.connect(kService, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                SLOT(handleResponse(uint, QVariantMap)));

    const FilterList filters{{tr("OpenVPN profiles"), {{0, "*.ovpn"}, {0, "*.conf"}}},
                             {tr("All files"), {{0, "*"}}}};
    QVariantMap options;
    options["handle_token"] = token;
    options["modal"] = true;
    options["filters"] = QVariant::fromValue(filters);
    options["current_folder"] = QVariant::fromValue(QDir::homePath().toUtf8() + '\0');

    auto message = QDBusMessage::createMethodCall(kService, kPath, "org.freedesktop.portal.FileChooser", "OpenFile");
    message << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            emit failed(tr("The file chooser could not be opened: %1").arg(reply.error().message()));
            return;
        }
        // Older portals pick their own path; follow it if it differs.
        const QString path = reply.value().path();
        if (path != m_requestPath) {
            auto bus = QDBusConnection::sessionBus();
            bus.disconnect(kService, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                           SLOT(handleResponse(uint, QVariantMap)));
            m_requestPath = path;
            bus.connect(kService, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                        SLOT(handleResponse(uint, QVariantMap)));
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    QDBusConnection::sessionBus().disconnect(kService, m_requestPath, "org.freedesktop.portal.Request", "Response",
                                             this, SLOT(handleResponse(uint, QVariantMap)));
    m_requestPath.clear();
    const QStringList uris = results.value("uris").toStringList();
    if (response != 0 || uris.isEmpty()) {
        emit cancelled();
        return;
    }
    emit picked(QUrl(uris.first()).toLocalFile());
}
