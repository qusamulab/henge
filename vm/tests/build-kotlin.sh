#!/bin/sh
# Builds vm/tests/kttest.dex: Kotlin test code + the parts of kotlin-stdlib it uses, shrunk by R8
# against the henge core classes.
set -e
cd "$(dirname "$0")"
. ../../scripts/java-env.sh
K=build/kotlin
rm -rf $K && mkdir -p $K/classes $K/dex
kotlinc kotlin/KtTest.kt $KOTLINC_FLAGS -cp "$KOTLIN_STDLIB" -d $K/classes
cat > $K/keep.pro <<'PRO'
-keep class kt.KtTestKt { public static int run(); }
-dontobfuscate
-dontoptimize
-ignorewarnings
PRO
jar cf $K/core.jar -C ../core/build/stubclasses .
jar cf $K/app.jar -C $K/classes .
java -cp "$D8JAR" com.android.tools.r8.R8 --release --min-api 24 --lib $K/core.jar --pg-conf $K/keep.pro \
    --output $K/dex $K/app.jar "$KOTLIN_STDLIB"
cp $K/dex/classes.dex kttest.dex
echo "kttest.dex: $(wc -c < kttest.dex) bytes"
