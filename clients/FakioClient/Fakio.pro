#-------------------------------------------------
#
# Project created by QtCreator 2014-03-03T12:48:21
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = Fakio
TEMPLATE = app


SOURCES += main.cpp\
    fakioconfig.cpp \
    fakiowindow.cpp

HEADERS  += \
    fakioconfig.h \
    fakiowindow.h

FORMS    += \
    fakiowindow.ui

RESOURCES += \
    Fakio.qrc

OTHER_FILES += \
    fakio.rc

RC_FILE = \
    fakio.rc
