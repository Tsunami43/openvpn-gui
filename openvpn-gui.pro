QT += core gui widgets qml quick quickcontrols2 dbus network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += libsecret-1
TARGET = openvpn-gui
VERSION = 0.5.2
DEFINES += APP_VERSION=\\\"$$VERSION\\\"
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/filepicker.h \
    src/portalfilepicker.h \
    src/profiles.h \
    src/secretstore.h \
    src/shell.h \
    src/theme.h \
    src/translator.h \
    src/updater.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/portalfilepicker.cpp \
    src/profiles.cpp \
    src/secretstore.cpp \
    src/shell.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/updater.cpp

RESOURCES += src/resources.qrc
