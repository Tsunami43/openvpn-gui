#pragma once

#include <QVariantMap>

#include "filepicker.h"

// Opens the desktop's own file chooser through xdg-desktop-portal
// (org.freedesktop.portal.FileChooser), so the dialog matches the rest of
// Omarchy instead of being drawn by Qt.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);
    void openConfig(const QString &title) override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    QString m_requestPath;
};
