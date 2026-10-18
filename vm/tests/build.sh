#!/bin/sh
# Builds vm/tests/vmtest.dex (VmTest + AndroidTest) from vm/tests/src against the core and android
# classes (vm/core/build, vm/android/build).
set -e
cd "$(dirname "$0")"
. ../../scripts/java-env.sh
rm -rf build/java && mkdir -p build/java/classes build/java/dex
javac $JAVAC_FLAGS -bootclasspath ../core/build/stubclasses:../android/build/classes \
    -d build/java/classes $(find src -name '*.java')
jar cf build/java/core.jar -C ../core/build/classes .
"$D8" --release --min-api 24 --lib build/java/core.jar --output build/java/dex $(find build/java/classes -name '*.class')
cp build/java/dex/classes.dex vmtest.dex
echo "vmtest.dex: $(wc -c < vmtest.dex) bytes"
