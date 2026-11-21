#!/usr/bin/env bash
# Regression test for concurrent same-method requests in the vsomeip binding.
#
# Usage: ./run.sh                    # build into ./build and check every scenario
#        VSOMEIP_PREFIX=... ./run.sh
#        TSAN=1 ./run.sh             # build/run under ThreadSanitizer (并发改动必须过这一档)
#        ASAN=1 ./run.sh             # build/run under AddressSanitizer + UBSan
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR=$(cd "$(dirname "$0")" && pwd)
BUILD="$DIR/build"
CXX=${CXX:-g++}

# Sanitizer 档位：同一份测试代码，换一种检测手段
SAN_FLAGS=""
if [ "${TSAN:-0}" = "1" ]; then
    SAN_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
elif [ "${ASAN:-0}" = "1" ]; then
    SAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi

# Locate a vsomeip build/install: library first, then its headers.
PREFIX=${VSOMEIP_PREFIX:-}
if [ -z "$PREFIX" ]; then
    for candidate in "$ROOT/build-fix/third_party/vsomeip" "$ROOT/build/third_party/vsomeip" \
                     "$HOME/Desktop/vsomeip/install"; do
        if [ -f "$candidate/libvsomeip3.so" ] || [ -f "$candidate/lib/libvsomeip3.so" ]; then
            PREFIX="$candidate"
            break
        fi
    done
fi
if [ -z "$PREFIX" ]; then
    echo "vsomeip not found; set VSOMEIP_PREFIX to a tree containing libvsomeip3.so" >&2
    exit 1
fi

LIBDIR="$PREFIX"
[ -d "$PREFIX/lib" ] && LIBDIR="$PREFIX/lib"
INCDIR="$PREFIX/include"
[ -d "$PREFIX/interface/vsomeip" ] && INCDIR="$PREFIX/interface"
[ -d "$ROOT/third_party/vsomeip/interface/vsomeip" ] && INCDIR="$ROOT/third_party/vsomeip/interface"

mkdir -p "$BUILD"
echo "== building (vsomeip prefix: $PREFIX)"
"$CXX" -std=c++17 -O1 -g -pthread -DCOM_WITH_VSOMEIP $SAN_FLAGS \
    -I"$ROOT/src/com/include" \
    -I"$ROOT/src/common/ara-core/include" \
    -I"$INCDIR" \
    -o "$BUILD/com_concurrency_test" \
    "$DIR/main.cpp" \
    "$ROOT/src/com/src/api/runtime.cpp" \
    "$ROOT/src/com/src/binding/bind_runtime_factory.cpp" \
    "$ROOT/src/com/src/binding/vsomeip/vsomeip_bind_runtime.cpp" \
    "$ROOT/src/com/src/binding/vsomeip/vsomeip_bind_handle.cpp" \
    "$ROOT/src/com/src/binding/vsomeip/vsomeip_bind_skeleton.cpp" \
      "$ROOT/src/com/src/binding/vsomeip/vsomeip_request_dispatcher.cpp" \
    "$ROOT/src/common/ara-core/src/abort.cpp" \
    "$ROOT/src/common/ara-core/src/initialize.cpp" \
    -L"$LIBDIR" -lvsomeip3 -Wl,-rpath,"$LIBDIR" || exit 1

export VSOMEIP_CONFIGURATION="$DIR/config.json"
if [ -n "$SAN_FLAGS" ]; then
    echo "== sanitizer: $SAN_FLAGS"
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=0 log_path=$BUILD/tsan}"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0 log_path=$BUILD/asan}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1 log_path=$BUILD/ubsan}"
    rm -f "$BUILD"/tsan.* "$BUILD"/asan.* "$BUILD"/ubsan.* 2>/dev/null || true
fi
ROUTING_SOCKET="/tmp/vsomeip-$(id -u)"
FAILED=0

stop_all() {
    pkill -f com_concurrency_test >/dev/null 2>&1
    sleep 1
}

check() {
    local scenario="$1" expect_match="$2" expect_fail="$3" log="$4"
    local matches fails
    matches=$(grep -c " MATCH$" "$log" || true)
    fails=$(grep -c " FAIL " "$log" || true)
    if [ "$matches" = "$expect_match" ] && [ "$fails" = "$expect_fail" ]; then
        echo "   PASS $scenario (match=$matches fail=$fails)"
    else
        echo "   FAIL $scenario (match=$matches fail=$fails, expected $expect_match/$expect_fail)"
        sed 's/^/      /' "$log"
        FAILED=1
    fi
}

for scenario in seq diff same steal cross async late shutdown; do
    echo "== scenario: $scenario"
    stop_all
    rm -f "$ROUTING_SOCKET"
    "$BUILD/com_concurrency_test" server > "$BUILD/server-$scenario.log" 2>&1 &
    SERVER_PID=$!
    sleep 3
    timeout 25 "$BUILD/com_concurrency_test" client "$scenario" > "$BUILD/client-$scenario.log" 2>&1
    kill "$SERVER_PID" >/dev/null 2>&1
    wait "$SERVER_PID" 2>/dev/null
    grep -E "^\[client\]" "$BUILD/client-$scenario.log"

    case "$scenario" in
        steal|late) check "$scenario" 1 1 "$BUILD/client-$scenario.log" ;;
        *)     check "$scenario" 2 0 "$BUILD/client-$scenario.log" ;;
    esac
done

stop_all
if [ "$FAILED" -ne 0 ]; then
    echo "RESULT: FAILED"
    exit 1
fi
echo "RESULT: PASSED"
