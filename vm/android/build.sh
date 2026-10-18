#!/bin/sh
# Builds henge-android.dex: the android.* classes henge-vm provides (compiled against the core classes).
set -e
cd "$(dirname "$0")"
. ../../scripts/java-env.sh
rm -rf build && mkdir -p build/classes build/dex
javac $JAVAC_FLAGS -bootclasspath ../core/build/stubclasses -d build/classes $(find src -name '*.java')
jar cf build/core.jar -C ../core/build/stubclasses .
"$D8" --release --min-api 24 --lib build/core.jar --output build/dex $(find build/classes -name '*.class')
cp build/dex/classes.dex henge-android.dex
echo "henge-android.dex: $(wc -c < henge-android.dex) bytes, $(find build/classes -name '*.class' | wc -l | tr -d ' ') classes"
