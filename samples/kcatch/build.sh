#!/bin/sh
# Builds samples/kcatch/kcatch.dex: the Kotlin sources + the parts of kotlin-stdlib they use, shrunk by R8
# against henge's own core / android classes (so a missing API shows up as a warning here).
set -e
cd "$(dirname "$0")"
. ../../scripts/java-env.sh
rm -rf build && mkdir -p build/classes build/dex
kotlinc src/*.kt $KOTLINC_FLAGS -cp "$KOTLIN_STDLIB:$ANDROID_JAR" -d build/classes
cat > build/keep.pro <<'PRO'
-keep class dev.henge.kcatch.MainActivity { <init>(); }
-dontobfuscate
-dontoptimize
-ignorewarnings
PRO
jar cf build/app.jar -C build/classes .
jar cf build/henge.jar -C ../../vm/core/build/stubclasses . -C ../../vm/android/build/classes .
java -cp "$D8JAR" com.android.tools.r8.R8 --release --min-api 24 --lib build/henge.jar --pg-conf build/keep.pro \
    --output build/dex build/app.jar "$KOTLIN_STDLIB"
cp build/dex/classes.dex kcatch.dex
echo "kcatch.dex: $(wc -c < kcatch.dex) bytes"
