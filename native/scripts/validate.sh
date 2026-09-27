#!/usr/bin/env bash
set -euo pipefail
work=${1:?Expected temporary build directory}
project=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$project/.." && pwd)
sdk=${2:-"$repo/sdk"}
cmake_bin=${CMAKE_BIN:-cmake}
python_bin=${PYTHON_BIN:-python3}
reports="$repo/validation/native"
mkdir -p "$reports" "$work/test-classes"
db="$repo/data/processed/v2/pluscode_admin_v2.sqlite"
if [[ "$cmake_bin" == */* ]]; then ctest_bin="$(dirname "$cmake_bin")/ctest"; else ctest_bin=ctest; fi
"$ctest_bin" --test-dir "$work/build-linux" --output-on-failure
"$python_bin" -B "$project/tests/differential.py" --library "$sdk/linux-x86_64/lib/libpluscode_admin.so" \
  --db "$db" --temp "$work/fixtures" --all-tiles --out "$reports/differential.json"
: "${JAVA_HOME:?Set JAVA_HOME}"
"$JAVA_HOME/bin/javac" --release 8 -encoding UTF-8 -cp "$sdk/java/pluscode-admin-1.0.0.jar" \
  -d "$work/test-classes" "$project/tests/JniSmoke.java"
"$JAVA_HOME/bin/java" -Xcheck:jni "-Djava.library.path=$sdk/linux-x86_64/lib" \
  -cp "$sdk/java/pluscode-admin-1.0.0.jar:$work/test-classes" JniSmoke "$db" "$work/fixtures/fixture.sqlite"
if [[ -n "${KOTLINC:-}" ]]; then
  "$KOTLINC" -jvm-target 1.8 -cp "$sdk/java/pluscode-admin-1.0.0.jar" \
    "$project/examples/KotlinExample.kt" -include-runtime -d "$work/kotlin-example.jar"
  "$JAVA_HOME/bin/java" -Xcheck:jni "-Djava.library.path=$sdk/linux-x86_64/lib" \
    -cp "$sdk/java/pluscode-admin-1.0.0.jar:$work/kotlin-example.jar" KotlinExampleKt "$db"
fi
g++ -std=c++17 "$project/examples/query.cpp" -I"$sdk/include" -L"$sdk/linux-x86_64/lib" \
  -Wl,-rpath,"$sdk/linux-x86_64/lib" -lpluscode_admin -o "$work/gcc-consumer"
"$work/gcc-consumer" "$db" 39.9042 116.4074
gcc "$project/examples/query.c" -I"$sdk/include" -L"$sdk/linux-x86_64/lib" \
  -Wl,-rpath,"$sdk/linux-x86_64/lib" -lpluscode_admin -o "$work/c-consumer"
"$work/c-consumer" "$db"
"$work/build-linux/pcad-memory-probe" "$db" > "$reports/memory.jsonl"
"$python_bin" -B "$project/scripts/record_runtime.py" "$sdk" "$reports/runtime.json" "${KOTLINC:-}"
echo 'Native, differential, Java, and external GCC consumer validation completed.'
