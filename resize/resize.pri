HEADERS += \
	$$PWD/cimageresizer.h \
	$$PWD/cpu_cache.h \
	$$PWD/qimage_resize.h \
	$$PWD/resize_internal.h \
	$$PWD/simd_primitives_avx2.h \
	$$PWD/simd_primitives_neon.h \
	$$PWD/simd_primitives_sse41.h \
	$$PWD/simd_support.h

# The SSE4.1 kernels need no compiler switch: MSVC compiles SSE4.1 intrinsics anywhere, GCC and Clang take the target attribute
SOURCES += \
	$$PWD/cimageresizer.cpp \
	$$PWD/cimageresizer_simd_neon.cpp \
	$$PWD/cimageresizer_simd_sse41.cpp \
	$$PWD/cpu_cache.cpp

# Each cimageresizer_simd_<level>.cpp compiles cimageresizer_simd.inl for its instruction set.
# Each compiles to nothing on other architectures, so none is listed conditionally: a macOS universal build compiles every TU for both.
OTHER_FILES += $$PWD/cimageresizer_simd.inl

# The AVX2 kernels need every intrinsic VEX-encoded, the 128-bit ones included: a legacy SSE encoding stalls for
# tens of cycles per instruction whenever the process left the upper YMM state dirty, which any AVX-using host
# does. GCC and Clang, clang-cl included, get that per function from the target attribute; MSVC only has the per-TU
# switch, so its kernels compile as a separate object. Flags are expanded here rather than taken from $(CXXFLAGS)
# because the VS project generator emits fully expanded command lines and has no makefile macros.
# The linker keeps one copy of each inline function, possibly this object's AVX2 one: CI runs the tests on an emulated CPU without AVX2 to catch it executing.
*msvc*:!contains(QMAKE_COMPILER, clang_cl) {
	# Keeps jumps off 32-byte boundaries: without it the short upscale loops run 5-10% slower on Alder Lake at some code placements
	QMAKE_CXXFLAGS += /QIntel-jcc-erratum

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
	# The VS project generator scans no includes for an extra compiler: only these reach the rule's inputs
	avx2Compiler.depends = $$PWD/cimageresizer_simd.inl $$PWD/simd_primitives_avx2.h $$PWD/simd_support.h $$PWD/resize_internal.h $$PWD/cimageresizer.h
	avx2Compiler.variable_out = OBJECTS
	avx2Compiler.output = $${OBJECTS_DIR}/${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
	# The generator supplies /Fd to its own compile rules but not to this one, and /Zi without it writes the PDB to
	# the build's working directory, where the linker will not find it and drops this object's symbols (LNK4099).
	avx2Compiler.commands = $$QMAKE_CXX -c $$AVX2_CXXFLAGS -Fd$$shell_quote($${OBJECTS_DIR}/) ${QMAKE_FILE_IN} -Fo${QMAKE_FILE_OUT}
	QMAKE_EXTRA_COMPILERS += avx2Compiler

	AVX2_SOURCES += $$PWD/cimageresizer_simd_avx2.cpp
	OTHER_FILES += $$PWD/cimageresizer_simd_avx2.cpp
} else {
	SOURCES += $$PWD/cimageresizer_simd_avx2.cpp
}
