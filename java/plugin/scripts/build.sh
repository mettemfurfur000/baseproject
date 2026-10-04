#!/usr/bin/env bash
# Builds the plugin, with the native shim compiled in first.
#
# The shim is a compiled artifact, so it cannot be produced by the Java build
# alone. This script runs `make abi` at the repository root, then stages the
# resulting shared library as a jar resource under the name the plugin's
# NativeLib looks for.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
plugin="$root/java/plugin"

# Java 25 is required to compile against Paper 26.1.2 and to use FFM. Prefer
# JAVA_HOME when it is already correct, otherwise fall back to a known install.
if [[ -z "${JAVA_HOME:-}" || ! -x "$JAVA_HOME/bin/javac" ]]; then
    for candidate in "/c/Program Files/Java/jdk-25" "/c/Program Files/Java/jdk-25.0.2"; do
        if [[ -x "$candidate/bin/javac" ]]; then
            JAVA_HOME="$candidate"
            export JAVA_HOME
            break
        fi
    done
fi

if [[ -z "${JAVA_HOME:-}" || ! -x "$JAVA_HOME/bin/javac" ]]; then
    echo "error: no Java 25 JDK found. Set JAVA_HOME to one." >&2
    exit 1
fi

echo "==> JDK: $("$JAVA_HOME/bin/javac" -version 2>&1)"

echo "==> building native shim"
make -C "$root" abi

# Name the library the way the plugin expects to find it, which encodes OS and
# architecture so a wrong-arch jar fails with a clear message instead of
# crashing the server.
stage_dir="$plugin/src/main/resources/native"
mkdir -p "$stage_dir"

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) suffix=".dll"; os_tag="windows" ;;
    Darwin*)                      suffix=".dylib"; os_tag="macos" ;;
    *)                           suffix=".so"; os_tag="linux" ;;
esac

case "$(uname -m)" in
    x86_64 | amd64) arch_tag="x64" ;;
    aarch64 | arm64) arch_tag="aarch64" ;;
    *) arch_tag="$(uname -m | tr -cd 'a-z0-9')" ;;
esac

prefix=""
[[ "$suffix" == ".so" ]] && prefix="lib"

shopt -s nullglob
built=("$root"/build/"$prefix"griefprot_ffi."$suffix")
if [[ ${#built[@]} -eq 0 ]]; then
    echo "error: make abi produced no shared library in $root/build" >&2
    exit 1
fi

target="$stage_dir/griefprot_${os_tag}_${arch_tag}${suffix}"
cp "${built[0]}" "$target"
echo "==> staged $(basename "${built[0]}") as ${target#"$plugin"/}"

echo "==> building plugin"
cd "$plugin"
if [[ -x ./mvnw ]]; then
    ./mvnw clean "$@"
else
    mvn clean "$@"
fi