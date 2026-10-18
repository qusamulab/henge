#!/bin/sh
# Builds samples/kcatch/kcatch.dex: the Kotlin sources + the parts of kotlin-stdlib they use, shrunk by R8
# against henge's own core / android classes (so a missing API shows up as a warning here).
set -e
cd "$(dirname "$0")"
JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}
export PATH="$JAVA_HOME/bin:$PATH"
ANDROID_HOME=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}
D8JAR=$(ls -d "$ANDROID_HOME"/build-tools/*/lib/d8.jar | tail -1)
ANDROID_JAR=$(ls -d "$ANDROID_HOME"/platforms/android-*/android.jar | tail -1)
STDLIB=$(ls /opt/homebrew/Cellar/kotlin/*/libexec/lib/kotlin-stdlib.jar | tail -1)
rm -rf build && mkdir -p build/classes build/dex
kotlinc src/*.kt -no-stdlib -cp "$STDLIB:$ANDROID_JAR" -d build/classes -jvm-target 1.8 \
    -Xno-param-assertions -Xno-call-assertions -Xno-receiver-assertions 2>&1 | grep -v "^warning" || true
cat > build/keep.pro <<'PRO'
-keep class dev.henge.kcatch.MainActivity { <init>(); }
-dontobfuscate
-dontoptimize
-ignorewarnings
PRO
jar cf build/app.jar -C build/classes .
jar cf build/henge.jar -C ../../vm/core/build/stubclasses . -C ../../vm/android/build/classes .
java -cp "$D8JAR" com.android.tools.r8.R8 --release --min-api 24 --lib build/henge.jar --pg-conf build/keep.pro \
    --output build/dex build/app.jar "$STDLIB" 2>&1 | grep -E "Missing (method|field)|error" | head -20 || true
cp build/dex/classes.dex kcatch.dex
echo "kcatch.dex: $(wc -c < kcatch.dex) bytes"
