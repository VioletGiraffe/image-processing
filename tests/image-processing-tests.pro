TEMPLATE = app
TARGET = image-processing-tests

CONFIG += console testcase strict_c++ c++2b
CONFIG -= c++17 c++2a

CONFIG(release, debug|release):CONFIG += optimize_full

QT += gui

DEFINES += CATCH_CONFIG_ENABLE_BENCHMARKING

CONFIG(debug, debug|release) {
	OUTPUT_DIR=debug
	DEFINES += _DEBUG
} else {
	OUTPUT_DIR=release
	DEFINES += NDEBUG=1
}

DESTDIR = $$PWD/bin/$${OUTPUT_DIR}
OBJECTS_DIR = $$PWD/build/$${OUTPUT_DIR}

win* {
	QMAKE_CXXFLAGS += /MP /Zi /FS /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = /W4
}

linux* | mac* | freebsd {
	QMAKE_CXXFLAGS += -std=c++2b
	QMAKE_CXXFLAGS_WARN_ON = -Wall
}

macx {
	contains(QMAKE_HOST.arch, arm64)|contains(QMAKE_APPLE_DEVICE_ARCHS, arm64) {
		QMAKE_CXXFLAGS += -include arm_acle.h
	}
}

INCLUDEPATH += \
	$$PWD/.. \
	$$PWD/../../cpp-template-utils \
	$$PWD/../../cpputils

HEADERS += \
	../resize/cimageresizer.h

SOURCES += \
	main.cpp \
	cimageresizer_benchmarks.cpp \
	cimageresizer_tests.cpp \
	../resize/cimageresizer.cpp \
	../../cpputils/assert/advanced_assert.cpp \
	../../cpputils/debugger/debugger_is_attached.cpp
