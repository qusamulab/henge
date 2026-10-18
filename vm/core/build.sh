#!/bin/sh
# Builds henge-core.dex: the java.* / android.* classes the VM provides itself.
# Needs a JDK (javac) and the Android SDK build tools (d8) + a platform android.jar.
set -e
cd "$(dirname "$0")"
JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}
export PATH="$JAVA_HOME/bin:$PATH"
ANDROID_HOME=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}
D8=$(ls -d "$ANDROID_HOME"/build-tools/*/d8 | tail -1)
JAR=$(ls -d "$ANDROID_HOME"/platforms/android-*/android.jar | tail -1)
rm -rf build && mkdir -p build/classes build/dex
# The core classes carry their real names (java.lang.Object, ...): compile them against nothing but
# themselves (an empty bootclasspath), as the JDK / Android libcore builds do.
mkdir -p build/empty
javac -source 8 -target 8 -bootclasspath build/empty -nowarn -Xlint:-options -XDstringConcat=inline \
    -d build/classes $(find src stubs -name '*.java')
# the stubs only let javac compile lambdas; D8 desugars them away
rm -rf build/stubclasses && cp -R build/classes build/stubclasses
rm -rf build/classes/java/lang/invoke
"$D8" --release --min-api 24 --output build/dex $(find build/classes -name '*.class')
cp build/dex/classes.dex henge-core.dex
echo "henge-core.dex: $(wc -c < henge-core.dex) bytes, $(find build/classes -name '*.class' | wc -l | tr -d ' ') classes"
