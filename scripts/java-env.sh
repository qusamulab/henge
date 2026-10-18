# Sourced by the dex build scripts (vm/*/build.sh, vm/tests/build*.sh, samples/kcatch/build.sh):
# puts a JDK on PATH and locates the newest Android SDK build tools / platform and kotlin-stdlib.
JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}
export PATH="$JAVA_HOME/bin:$PATH"
ANDROID_HOME=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}
D8=$(ls -d "$ANDROID_HOME"/build-tools/*/d8 2>/dev/null | tail -1)
D8JAR=$(ls -d "$ANDROID_HOME"/build-tools/*/lib/d8.jar 2>/dev/null | tail -1)
ANDROID_JAR=$(ls -d "$ANDROID_HOME"/platforms/android-*/android.jar 2>/dev/null | tail -1)
KOTLIN_STDLIB=$(ls /opt/homebrew/Cellar/kotlin/*/libexec/lib/kotlin-stdlib.jar 2>/dev/null | tail -1)

# Java 8 source level; string concatenation inlined (henge-vm has no invokedynamic).
JAVAC_FLAGS="-source 8 -target 8 -nowarn -Xlint:-options -XDstringConcat=inline"
KOTLINC_FLAGS="-no-stdlib -jvm-target 1.8 -Xno-param-assertions -Xno-call-assertions -Xno-receiver-assertions"
