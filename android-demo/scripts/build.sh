#!/usr/bin/env bash
set -euo pipefail
base="$(cd "$(dirname "$0")/.." && pwd)"
project="$base/project"
task="${DEMO_WORK_ROOT:-${TMPDIR:-/tmp}/shengshixian4pluscode-android}"
: "${JAVA_HOME:?Set JAVA_HOME to JDK 17 or newer}"
: "${ANDROID_HOME:?Set ANDROID_HOME to an Android SDK containing Platform and Build Tools 35}"
export ANDROID_USER_HOME="${ANDROID_USER_HOME:-$task/android-home}"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$task/gradle-home}"
export DEMO_BUILD_ROOT="${DEMO_BUILD_ROOT:-$task/build}"
export PATH="$JAVA_HOME/bin:$PATH"
gradle="${GRADLE_BIN:-$project/gradlew}"
"$gradle" -p "$project" --project-cache-dir "$task/project-cache" --no-daemon --console=plain "$@"
