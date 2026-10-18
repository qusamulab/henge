#!/bin/sh
# Builds henge-android.dex: the android.* classes henge-vm provides (compiled against the core classes).
set -e
cd "$(dirname "$0")"
JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}
export PATH="$JAVA_HOME/bin:$PATH"
ANDROID_HOME=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}
D8=$(ls -d "$ANDROID_HOME"/build-tools/*/d8 | tail -1)
rm -rf build && mkdir -p build/classes build/dex
javac -source 8 -target 8 -bootclasspath ../core/build/stubclasses -nowarn -Xlint:-options -XDstringConcat=inline \
    -d build/classes $(find src -name '*.java')
jar cf build/core.jar -C ../core/build/stubclasses .
"$D8" --release --min-api 24 --lib build/core.jar --output build/dex $(find build/classes -name '*.class')
cp build/dex/classes.dex henge-android.dex
echo "henge-android.dex: $(wc -c < henge-android.dex) bytes, $(find build/classes -name '*.class' | wc -l | tr -d ' ') classes"
