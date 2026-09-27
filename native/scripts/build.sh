#!/usr/bin/env bash
set -euo pipefail
# Usage: build.sh linux|android|java|all ABSOLUTE_TEMP_DIR [ABSOLUTE_OUTPUT_DIR]
mode=${1:?Expected linux, android, java, or all}
work=${2:?Expected an absolute temporary build directory}
project=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$project/.." && pwd)
out=${3:-"$repo/sdk"}
case "$work" in /*) ;; *) echo 'Build directory must be absolute' >&2; exit 2;; esac
case "$out" in /*) ;; *) echo 'Output directory must be absolute' >&2; exit 2;; esac
case "$mode" in linux|android|java|all) ;; *) echo 'Unknown build mode' >&2; exit 2;; esac
case "$work/" in "$project/"*) echo 'Build outside the source tree' >&2; exit 2;; esac
cmake_bin=${CMAKE_BIN:-cmake}
jobs=${PCAD_JOBS:-4}
mkdir -p "$work" "$out"
export TMPDIR="$work/tmp"
mkdir -p "$TMPDIR"
if [[ "$mode" == linux || "$mode" == all ]]; then
  extra=()
  if [[ -n "${PCAD_ZIG:-}" ]]; then
    export ZIG_GLOBAL_CACHE_DIR="$work/zig-cache"
    export ZIG_LOCAL_CACHE_DIR="$work/zig-local-cache"
    extra+=("-DCMAKE_TOOLCHAIN_FILE=$project/cmake/linux-portable.cmake" "-DPCAD_ZIG=$PCAD_ZIG")
  fi
  "$cmake_bin" -S "$project" -B "$work/build-linux" -DCMAKE_BUILD_TYPE=Release \
    "-DPCAD_TEST_DB=$repo/data/processed/v2/pluscode_admin_v2.sqlite" "${extra[@]}"
  "$cmake_bin" --build "$work/build-linux" -j "$jobs"
  "$cmake_bin" --install "$work/build-linux" --prefix "$out/linux-x86_64" --strip
fi
if [[ "$mode" == android || "$mode" == all ]]; then
  : "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the extracted NDK}"
  for abi in ${PCAD_ANDROID_ABIS:-arm64-v8a x86_64 armeabi-v7a}; do
    "$cmake_bin" -S "$project" -B "$work/build-android-$abi" -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
      "-DANDROID_ABI=$abi" -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static -DPCAD_TESTS=OFF
    "$cmake_bin" --build "$work/build-android-$abi" -j "$jobs"
    mkdir -p "$out/android/jniLibs/$abi"
    cp "$work/build-android-$abi/libpluscode_admin.so" "$out/android/jniLibs/$abi/"
    "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded "$out/android/jniLibs/$abi/libpluscode_admin.so"
  done
fi
if [[ "$mode" == java || "$mode" == all ]]; then
  : "${JAVA_HOME:?Set JAVA_HOME to JDK 11 or newer}"
  mkdir -p "$work/java-classes" "$work/jar-resources/META-INF/proguard" "$out/java"
  "$JAVA_HOME/bin/javac" --release 8 -encoding UTF-8 -d "$work/java-classes" \
    "$project/java/com/askcodex/pluscode/PlusCodeIndex.java"
  cp "$project/consumer-rules.pro" "$work/jar-resources/META-INF/proguard/pluscode-admin.pro"
  "$JAVA_HOME/bin/jar" cf "$out/java/pluscode-admin-1.0.0.jar" -C "$work/java-classes" . -C "$work/jar-resources" .
  "$JAVA_HOME/bin/jar" cf "$out/java/pluscode-admin-1.0.0-sources.jar" -C "$project/java" .
fi
mkdir -p "$out/include"
cp -R "$project/include/pluscode_admin" "$out/include/"
cp "$project/consumer-rules.pro" "$out/"
