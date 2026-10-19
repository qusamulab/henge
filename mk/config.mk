# ---- configuration (override on the command line) ----
CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2
CPPFLAGS += -Iinclude
HENGE_CFLAGS := -std=c11 -Wall -Wextra
# Compiler-generated dependencies follow the headers each translation unit actually uses.
COMPILE.c = $(CC) $(CPPFLAGS) $(HENGE_CFLAGS) $(CFLAGS) -MMD -MP -MF $@.d -MT $@

NDK_ROOT  ?= /opt/homebrew/share/android-ndk
NDK_CC    ?= $(NDK_ROOT)/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang
NDK_CXX   ?= $(NDK_ROOT)/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang++
NDK_FLAGS := --target=aarch64-linux-android28 -fPIC -shared -O2 -Wl,-z,max-page-size=16384

# Android SDK, for aapt2 + android.jar (sample APK). Newest installed versions by default.
ANDROID_HOME ?= $(HOME)/android-sdk
AAPT2        ?= $(lastword $(sort $(wildcard $(ANDROID_HOME)/build-tools/*/aapt2)))
ANDROID_JAR  ?= $(lastword $(sort $(wildcard $(ANDROID_HOME)/platforms/android-*/android.jar)))

# CONVERT_FLAGS: extra henge-convert flags (e.g. --libsystem)
# PLATFORM:      app-dylib target, iossim (simulator) or ios (device)
# SIM:           simulator device name for app / app-run
# APP_TIMEOUT:   app-run: seconds to wait for the m1 summary line
CONVERT_FLAGS ?=
PLATFORM      ?= iossim
SIM           ?= iPhone 17
APP_TIMEOUT   ?= 60

B ?= build
