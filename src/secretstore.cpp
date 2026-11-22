// GLib names a struct member "signals", which Qt defines as a keyword, so
// libsecret is included before any Qt header and the keyword restored after.
#undef signals
#include <libsecret/secret.h>
#define signals Q_SIGNALS

#include "secretstore.h"

#include <QByteArray>

namespace {

const SecretSchema *schema() {
    static const SecretSchema value = {
        "org.omarchy.openvpn-gui.Profile",
        SECRET_SCHEMA_NONE,
        {{"profile", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SecretSchemaAttributeType(0)}},
        0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    return &value;
}

} // namespace

bool KeyringStore::store(const QString &profileId, const QString &label, const QString &password, QString *error) {
    GError *gerror = nullptr;
    const QByteArray id = profileId.toUtf8(), name = label.toUtf8(), secret = password.toUtf8();
    const gboolean ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, name.constData(),
                                                   secret.constData(), nullptr, &gerror, "profile", id.constData(),
                                                   nullptr);
    if (!ok && error)
        *error = gerror ? QString::fromUtf8(gerror->message) : QStringLiteral("unknown keyring error");
    if (gerror)
        g_error_free(gerror);
    return ok;
}

QString KeyringStore::lookup(const QString &profileId) {
    const QByteArray id = profileId.toUtf8();
    gchar *secret = secret_password_lookup_sync(schema(), nullptr, nullptr, "profile", id.constData(), nullptr);
    if (!secret)
        return {};
    const QString value = QString::fromUtf8(secret);
    secret_password_free(secret);
    return value;
}

void KeyringStore::remove(const QString &profileId) {
    const QByteArray id = profileId.toUtf8();
    secret_password_clear_sync(schema(), nullptr, nullptr, "profile", id.constData(), nullptr);
}
