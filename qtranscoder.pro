CONFIG += warn_on
TEMPLATE = app
QT += core widgets

INCLUDEPATH += /usr/local/ffmpeg/include
LIBS += -L/usr/local/ffmpeg/lib -lavdevice -lavformat -lavfilter -lavcodec -lswresample -lswscale -lavutil

HEADERS += transcoder.h 

SOURCES += transcoder.cpp \
           main.cpp
