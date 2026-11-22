#pragma once

#include <QObject>
#include <QString>

// The open dialog behind an interface, so the backend never talks to D-Bus
// directly and tests can hand it a fake picker.
class FilePicker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~FilePicker() override = default;

    // Shows the dialog without blocking; exactly one of picked() or
    // cancelled() follows, or failed() when no dialog could be shown.
    virtual void openConfig(const QString &title) = 0;

signals:
    void picked(const QString &path);
    void cancelled();
    void failed(const QString &message);
};
