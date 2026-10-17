TARGET = kobo

TEMPLATE = lib
CONFIG += plugin c++17

DEFINES += QT_NO_FOREACH

QMAKE_CXXFLAGS += -Wno-missing-field-initializers

QMAKE_RPATHDIR += ../../lib

QT += core-private gui-private fb_support-private

lessThan(QT_MAJOR_VERSION, 6) {
    QT += eventdispatcher_support-private \
          service_support-private \
          fontdatabase_support-private
}

INCLUDEPATH += $$PWD/src

# FBInk
# Built as a static PIC archive by its own Makefile. The final Release/libfbink.a (or Debug/) already
# contains libi2c, and INPUT=1 adds fbink_input_scan, which we use to find the input devices.
FBINK_DIR = $$PWD/FBInk
DEFINES += FBINK_FOR_KOBO
INCLUDEPATH += $$FBINK_DIR

CONFIG(debug, debug|release) {
    FBINK_OUT_DIR = Debug
    FBINK_MAKE_FLAGS = DEBUG=1
} else {
    FBINK_OUT_DIR = Release
}
FBINK_LIB = $$FBINK_DIR/$$FBINK_OUT_DIR/libfbink.a

# Use the same cross compiler as the Qt kit, e.g. /path/to/arm-kobo-linux-gnueabihf-gcc -> /path/to/arm-kobo-linux-gnueabihf-
FBINK_CROSS_COMPILE = $$replace(QMAKE_CC, gcc$, )

fbink.target = $$FBINK_LIB
fbink.commands = $(MAKE) -C $$FBINK_DIR CROSS_COMPILE=$$FBINK_CROSS_COMPILE MINIMAL=1 DRAW=1 INPUT=1 KOBO=true $$FBINK_MAKE_FLAGS pic
# FBInk's own Makefile is incremental, so always give it a chance to rebuild
fbink.depends = FORCE
fbinkclean.commands = $(MAKE) -C $$FBINK_DIR $$FBINK_MAKE_FLAGS distclean
clean.depends += fbinkclean
QMAKE_EXTRA_TARGETS += fbink fbinkclean clean
PRE_TARGETDEPS += $$FBINK_LIB
LIBS += $$FBINK_LIB

SOURCES = src/main.cpp \
          src/dither.cpp \
          src/koboinputmanager.cpp \
          src/kobokeyhandler.cpp \
          src/kobotouchhandler.cpp \
          src/kobodevicedescriptor.cpp \
          src/kobofbscreen.cpp \
          src/koboplatformadditions.cpp \
          src/koboplatformintegration.cpp \
          src/kobowifimanager.cpp

HEADERS = \
          src/dither.h \
          src/einkenums.h \
          src/koboinputmanager.h \
          src/kobokeyhandler.h \
          src/kobotouchhandler.h \
          src/kobodevicedescriptor.h \
          src/kobofbscreen.h \
          src/koboplatformadditions.h \
          src/koboplatformfunctions.h \
          src/koboplatformintegration.h \
          src/kobowifimanager.h

QTDIR = /mnt/onboard/.adds/qt-linux-5.15-kde-kobo
greaterThan(QT_MAJOR_VERSION, 5): QTDIR = /mnt/onboard/.adds/qt-$${QT_VERSION}-kobo
target.path = $$QTDIR/plugins/platforms
INSTALLS += target

DISTFILES += \
    koboplatformplugin.json

RESOURCES += \
    Resources.qrc
