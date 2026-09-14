TEMPLATE = lib
CONFIG += staticlib
TARGET = image-processing

CONFIG -= qt
CONFIG += strict_c++

include(../global.pri)

INCLUDEPATH += \
	../cpp-template-utils

mac* | linux* | freebsd{
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR  = ../bin/$${OUTPUT_DIR}/
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

win*{
	QMAKE_CXXFLAGS += /MP /Zi /FS
	Debug:QMAKE_CXXFLAGS += /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS_WARN_ON += -pedantic-errors
	QMAKE_CFLAGS_WARN_ON += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON *= -Wall
	# SIMDe's non-target-attributed inline helpers are parsed without AVX; they are always_inline'd into our
	# target("avx2,fma") functions, and no vector type crosses a non-inlined boundary, so the ABI note is noise.
	*-g++*:QMAKE_CXXFLAGS_WARN_ON += -Wno-psabi

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}
include (resize/resize.pri)
