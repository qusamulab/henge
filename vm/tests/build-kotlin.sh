#!/bin/sh
# Builds vm/tests/kttest.dex: Kotlin test code + the parts of kotlin-stdlib it uses, shrunk by R8
# against the henge core classes.
set -e
cd "$(dirname "$0")"
JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}
export PATH="$JAVA_HOME/bin:$PATH"
ANDROID_HOME=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}
D8JAR=$(ls -d "$ANDROID_HOME"/build-tools/*/lib/d8.jar | tail -1)
STDLIB=$(ls /opt/homebrew/Cellar/kotlin/*/libexec/lib/kotlin-stdlib.jar | tail -1)
rm -rf kbuild && mkdir -p kbuild/classes kbuild/dex
kotlinc kotlin/KtTest.kt -no-stdlib -cp "$STDLIB" -d kbuild/classes -jvm-target 1.8 -Xno-param-assertions -Xno-call-assertions -Xno-receiver-assertions 2>&1 | grep -v "^warning" || true
cat > kbuild/keep.pro <<'PRO'
-keep class kt.KtTestKt { public static int run(); }
-dontobfuscate
-dontoptimize
-ignorewarnings
PRO
jar cf kbuild/core.jar -C ../core/build/stubclasses .
jar cf kbuild/app.jar -C kbuild/classes .
java -cp "$D8JAR" com.android.tools.r8.R8 --release --min-api 24 --lib kbuild/core.jar --pg-conf kbuild/keep.pro \
    --output kbuild/dex kbuild/app.jar "$STDLIB" 2>&1 | grep -v "^Warning" | head -30 || true
cp kbuild/dex/classes.dex kttest.dex
echo "kttest.dex: $(wc -c < kttest.dex) bytes"
