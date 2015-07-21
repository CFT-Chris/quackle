TEMPLATE = app
DEPENDPATH += .. ../quackleio
INCLUDEPATH += . ..

# enable/disable debug symbols
# CONFIG += debug
CONFIG += release

CONFIG += console
CONFIG -= x11
CONFIG -= app_bundle

debug {
  OBJECTS_DIR = obj/debug
}

release {
  OBJECTS_DIR = obj/release
}

LIBS += -lquackleio -lquackle

QMAKE_LFLAGS_RELEASE += -L../lib/release -L../quackleio/lib/release
QMAKE_LFLAGS_DEBUG += -L../lib/debug -L../quackleio/lib/debug

# Input
HEADERS += minidawgmaker.h
SOURCES += minidawgmaker.cpp makeminidawgmain.cpp


macx-g++ {
    QMAKE_CXXFLAGS += -fpermissive
}
