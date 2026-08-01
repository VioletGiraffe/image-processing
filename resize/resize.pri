HEADERS += \
	$$PWD/cimageresizer.h \
	$$PWD/resize_internal.h \
	$$PWD/simd_support.h

SOURCES += \
	$$PWD/cimageresizer.cpp

# The SIMD kernels need every intrinsic VEX-encoded, the 128-bit ones included: a legacy SSE encoding stalls for
# tens of cycles per instruction whenever the process left the upper YMM state dirty, which any AVX-using host
# does. GCC and Clang get that per function from the target attribute; MSVC only has the per-TU switch, so its
# kernels compile as a separate object. Flags are expanded here rather than taken from $(CXXFLAGS) because the
# VS project generator emits fully expanded command lines and has no makefile macros.
*msvc* {
	AVX2_CXXFLAGS = $$QMAKE_CXXFLAGS /arch:AVX2
	CONFIG(debug, debug|release): AVX2_CXXFLAGS += $$QMAKE_CXXFLAGS_DEBUG
	else: AVX2_CXXFLAGS += $$QMAKE_CXXFLAGS_RELEASE
	AVX2_CXXFLAGS += $$QMAKE_CXXFLAGS_WARN_ON $$QMAKE_CXXFLAGS_EXCEPTIONS_ON $$QMAKE_CXXFLAGS_RTTI_ON
	# Feature files append to QMAKE_CXXFLAGS after this .pri is evaluated, so flags from there must be repeated
	AVX2_CXXFLAGS += -utf-8
	for(avx2Define, DEFINES): AVX2_CXXFLAGS += -D$$avx2Define
	for(avx2IncludePath, INCLUDEPATH): AVX2_CXXFLAGS += -I$$shell_quote($$avx2IncludePath)

	avx2Compiler.name = AVX2 kernels
	avx2Compiler.input = AVX2_SOURCES
	avx2Compiler.dependency_type = TYPE_C
	avx2Compiler.variable_out = OBJECTS
	avx2Compiler.output = $${OBJECTS_DIR}/${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
	avx2Compiler.commands = $$QMAKE_CXX -c $$AVX2_CXXFLAGS ${QMAKE_FILE_IN} -Fo${QMAKE_FILE_OUT}
	QMAKE_EXTRA_COMPILERS += avx2Compiler

	AVX2_SOURCES += $$PWD/cimageresizer_simd.cpp
	OTHER_FILES += $$PWD/cimageresizer_simd.cpp
} else {
	SOURCES += $$PWD/cimageresizer_simd.cpp
}
