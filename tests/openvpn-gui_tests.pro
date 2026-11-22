QT += core gui quick quickcontrols2 testlib dbus network
CONFIG += c++17 testcase
DEFINES += SRCDIR=\\\"$$PWD\\\"
TARGET = openvpn-gui_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/filepicker.h ../src/profiles.h ../src/secretstore.h ../src/theme.h ../src/translator.h ../src/updater.h
SOURCES += openvpn-gui_tests.cpp ../src/backend.cpp ../src/profiles.cpp ../src/theme.cpp ../src/translator.cpp ../src/updater.cpp
