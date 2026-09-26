#!/bin/sh
#-----------------------------------------------------------------------------
# Build and run every test suite.
#
# The DSP, the WAV reader and the kit rules are SDK-free, so DspTests needs
# nothing but a compiler. StateTests needs the VST3 headers and a handful of
# base sources for the stream classes; it is skipped with a note rather than
# a failure if the SDK has not been fetched yet (./setup-xcode.sh does that).
#
#   tools/run-tests.sh            release sequence step 1
#
# Binaries go to a temporary directory and are thrown away, so nothing lands
# in the source tree.
#-----------------------------------------------------------------------------
set -e

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
CXX="${CXX:-c++}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

FAILED=0
S="$ROOT/source"
T="$ROOT/tests"

printf '\n===== dsp-tests\n'
if $CXX -std=c++17 -O2 -Wall -Wextra -I"$S" \
        "$T/DspTests.cpp" "$S/SampleDrumDsp.cpp" "$S/SampleDrumSample.cpp" \
        -o "$OUT/dsp-tests"; then
	"$OUT/dsp-tests" || { echo "FAILED: dsp-tests" >&2; FAILED=$((FAILED + 1)); }
else
	echo "FAILED TO BUILD: dsp-tests" >&2
	FAILED=$((FAILED + 1))
fi

# StateTests is the one suite that needs the SDK: the state is written through
# Steinberg's IBStream, and the test reads it back through the same class.
SDK="$ROOT/external/vst3sdk"
if [ -d "$SDK" ]; then
	printf '\n===== state-tests\n'
	B="$SDK/base/source"
	# ON macOS THIS NEEDS CoreFoundation. funknown.cpp and fstring.cpp are
	# partly wrapped in SMTG_OS_MACOS, so they link on Linux without it and
	# fail on the Mac with undefined _CFUUIDCreate - exactly the wrong way
	# round for a project released from a Mac and tested from Linux.
	SDK_LIBS="-lpthread"
	if [ "$(uname -s)" = "Darwin" ]; then
		SDK_LIBS="-framework CoreFoundation"
	fi
	# The build output is KEPT and printed when it fails; "FAILED TO BUILD"
	# alone says nothing about what.
	# shellcheck disable=SC2086
	if $CXX -std=c++17 -O2 -DRELEASE=1 -DNDEBUG -I"$SDK" -I"$S" \
	        "$T/StateTests.cpp" "$S/SampleDrumState.cpp" "$S/SampleDrumParams.cpp" \
	        "$S/SampleDrumDsp.cpp" "$S/SampleDrumSample.cpp" \
	        "$SDK/public.sdk/source/common/memorystream.cpp" \
	        "$B/fstreamer.cpp" "$B/fstring.cpp" "$B/fobject.cpp" "$B/fbuffer.cpp" \
	        "$B/updatehandler.cpp" "$B/fdebug.cpp" "$SDK/base/thread/source/flock.cpp" \
	        "$SDK/pluginterfaces/base/funknown.cpp" \
	        "$SDK/pluginterfaces/base/conststringtable.cpp" $SDK_LIBS \
	        -o "$OUT/state-tests" >"$OUT/state-build.log" 2>&1; then
		"$OUT/state-tests" || { echo "FAILED: state-tests" >&2; FAILED=$((FAILED + 1)); }
	else
		echo "FAILED TO BUILD: state-tests" >&2
		sed 's/^/    /' "$OUT/state-build.log" >&2
		FAILED=$((FAILED + 1))
	fi
else
	printf '\n===== state-tests\nskipped - no VST3 SDK at external/vst3sdk yet.\nRun ./setup-xcode.sh once to fetch it.\n'
fi

#-----------------------------------------------------------------------------
# The release kit: the version and maker's name agree everywhere, and the
# release scripts' own refusals still refuse. None of this needs a Mac.
#-----------------------------------------------------------------------------
printf '\n===== check-versions\n'
python3 "$ROOT/tools/check-versions.py" || FAILED=$((FAILED + 1))
for t in test-build-release test-publish; do
	printf '\n===== installer/%s\n' "$t"
	if [ -x "$ROOT/installer/$t.sh" ]; then
		"$ROOT/installer/$t.sh" || FAILED=$((FAILED + 1))
	else
		# A script that will not run is not a check that passed.
		echo "NOT EXECUTABLE: installer/$t.sh - chmod +x installer/*.sh"
		FAILED=$((FAILED + 1))
	fi
done

printf '\n'
if [ "$FAILED" -eq 0 ]; then
	echo "All suites passed."
	exit 0
fi
echo "$FAILED suite(s) FAILED."
exit 1
