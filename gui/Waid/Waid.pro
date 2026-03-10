QT += core gui widgets
CONFIG += c++17

SOURCES += \
    edgeitem.cpp \
    main.cpp \
    mainwindow.cpp \
    nodeitem.cpp \
    topologyview.cpp

HEADERS += \
    devicetype.h \
    edgeitem.h \
    mainwindow.h \
    nodeitem.h \
    topologyview.h

FORMS += \
    mainwindow.ui

# qextserialport einbinden
include(qextserialport/src/qextserialport.pri)
