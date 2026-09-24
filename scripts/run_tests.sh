#!/bin/sh

# Builds the tests with qmake and runs them, or with --benchmark runs the benchmarks instead and prints the ratio report.
# Exit code: non-zero on a build or test failure.
# Options, before any other argument:
#   --benchmark - benchmarks instead of tests
#   --debug     - the debug configuration; the default is release
#   --no-build  - runs what is already built
# The remaining arguments go to the Catch2 runner: a test spec narrows either mode, options such as --benchmark-samples 50 override the defaults.
# qmake comes from $QT_ROOT_DIR/bin when that is set, otherwise from PATH.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG=release
BENCHMARK=0
BUILD=1
while [ $# -gt 0 ]; do
	case "$1" in
		--benchmark) BENCHMARK=1 ;;
		--debug) CONFIG=debug ;;
		--no-build) BUILD=0 ;;
		*) break ;;
	esac
	shift
done

if [ "$BUILD" = 1 ]; then
	cd "$ROOT/tests"
	"${QT_ROOT_DIR:+$QT_ROOT_DIR/bin/}qmake" image-processing-tests.pro -config "$CONFIG" CONFIG+="$CONFIG"
	make -j"$(getconf _NPROCESSORS_ONLN)"
fi

BINARIES="$ROOT/tests/bin/$CONFIG"
EXECUTABLE="$BINARIES/image-processing-tests"
# macOS builds an app bundle
if [ -d "$EXECUTABLE.app" ]; then EXECUTABLE="$EXECUTABLE.app/Contents/MacOS/image-processing-tests"; fi

if [ "$BENCHMARK" = 0 ]; then
	exec "$EXECUTABLE" "$@"
fi

RESULTS="$BINARIES/benchmark-results.xml"
# Catch2 rejects a repeated option, so a default is prepended only when the caller did not pass that option
passed() {
	option="$1"
	shift
	for argument in "$@"; do
		if [ "$argument" = "$option" ]; then return 0; fi
	done
	return 1
}
if ! passed --benchmark-no-analysis "$@"; then set -- --benchmark-no-analysis "$@"; fi
if ! passed --benchmark-samples "$@"; then set -- --benchmark-samples 10 "$@"; fi
# QT_NO_GUI_THREADPOOL keeps QImage::scaled, the control, single-threaded
QT_NO_GUI_THREADPOOL=1 "$EXECUTABLE" "[!benchmark]" --reporter xml --out "$RESULTS" "$@"
exec python3 "$ROOT/tests/report_benchmark_ratios.py" "$RESULTS"
