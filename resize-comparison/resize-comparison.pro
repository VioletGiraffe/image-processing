TEMPLATE = app
TARGET = resize-comparison

CONFIG += strict_c++ c++2b
CONFIG -= c++17 c++2a

CONFIG(release, debug|release):CONFIG += optimize_full ltcg

QT += widgets

CONFIG(debug, debug|release) {
	OUTPUT_DIR=debug
	DEFINES += _DEBUG
} else {
	OUTPUT_DIR=release
	DEFINES += NDEBUG=1
}

DESTDIR = $$PWD/bin/$${OUTPUT_DIR}
OBJECTS_DIR = $$PWD/build/$${OUTPUT_DIR}
MOC_DIR = $$PWD/build/$${OUTPUT_DIR}

win* {
	QMAKE_CXXFLAGS += /MP /Zi /FS /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = /W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	# /OPT:REF and /OPT:ICF default to on only while /DEBUG is absent, so they must be restated alongside it.
	CONFIG(release, debug|release):QMAKE_LFLAGS += /DEBUG:FULL /OPT:REF /OPT:ICF
}

linux* | mac* | freebsd {
	QMAKE_CXXFLAGS += -std=c++2b
	QMAKE_CXXFLAGS_WARN_ON = -Wall
	# See the same flag in ../image-processing.pro
	*-g++*:QMAKE_CXXFLAGS += -Wno-psabi
}

macx {
	contains(QMAKE_HOST.arch, arm64)|contains(QMAKE_APPLE_DEVICE_ARCHS, arm64) {
		QMAKE_CXXFLAGS += -include arm_acle.h
	}
}

INCLUDEPATH += \
	$$PWD/.. \
	$$PWD/../../cpputils \
	$$PWD/../../cpp-template-utils

HEADERS += \
	cmainwindow.h \
	comparisonengine.h \
	cpixelviewerwidget.h \
	imagemetrics.h \
	qimageviewutils.h \
	testpatterns.h

SOURCES += \
	main.cpp \
	cmainwindow.cpp \
	comparisonengine.cpp \
	cpixelviewerwidget.cpp \
	imagemetrics.cpp \
	testpatterns.cpp \
	$${PWD}/../../cpputils/threading/cthreadpool.cpp \
	$${PWD}/../../cpputils/threading/thread_helpers.cpp \
	$${PWD}/../../cpputils/assert/advanced_assert.cpp

# Carries the resizer's sources, headers, and the per-file /arch:AVX2 rule its SIMD kernels need
include(../resize/resize.pri)
