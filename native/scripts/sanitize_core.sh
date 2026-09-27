#!/usr/bin/env bash
set -euo pipefail
work=${1:?Expected existing Linux build workspace}
project=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$project/.." && pwd)
deps="$work/build-linux/_deps"
mkdir -p "$work/sanitize"
export TMPDIR="$work/tmp"
sqlite=("$deps"/sqlite-*/sqlite-amalgamation-*)
xz=("$deps"/xz-*/xz-*)
zlib=("$deps"/zlib-*/zlib-*)
json=("$deps"/json-*/include)
olc=("$deps"/olc-*/open-location-code-*)
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
g++ "${flags[@]}" -fPIC -shared -DPCAD_BUILDING \
  -I"$project/include" -I"$project/src" -I"${sqlite[0]}" -I"${xz[0]}/src/liblzma/api" \
  -I"${zlib[0]}" -I"${json[0]}" -I"${olc[0]}/cpp" \
  "$project/src/index.cpp" "$project/src/c_api.cpp" "$deps/olc-corrected.cc" "${olc[0]}/cpp/codearea.cc" \
  "$work/build-linux/libpcad_sqlite.a" "$deps/xz-build/liblzma.a" "$deps/zlib-build/libz.a" \
  -ldl -pthread "-Wl,--version-script=$project/src/exports.map" -Wl,-z,defs \
  -o "$work/sanitize/libpluscode_admin.so"
g++ "${flags[@]}" -I"$project/include" -I"${json[0]}" "$project/tests/native_tests.cpp" \
  -L"$work/sanitize" -lpluscode_admin -pthread "-Wl,-rpath=$work/sanitize" -o "$work/sanitize/native-tests"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$work/sanitize/native-tests" "$repo/data/processed/v2/pluscode_admin_v2.sqlite"
echo 'Instrumented C++ reader/C ABI/OLC passed ASan/UBSan; bundled C dependencies are not instrumented.'
