# ---- samples (Android NDK) ----
samples: $(B)/samples/libadd.so $(B)/samples/libpatch.so $(B)/samples/libdep.so $(B)/samples/libshim.so ## build sample .so files with the NDK

$(B)/samples/libadd.so: samples/libadd/add.c
	@mkdir -p $(@D)
	$(NDK_CC) $(NDK_FLAGS) -o $@ $<

# libdep.so needs libbase.so; both use Android packed relocations (APS2).
PACKED := -Wl,--pack-dyn-relocs=android
$(B)/samples/libbase.so: samples/libdep/base.c
	@mkdir -p $(@D)
	$(NDK_CC) $(NDK_FLAGS) $(PACKED) -Wl,-soname,libbase.so -o $@ $<

$(B)/samples/libdep.so: samples/libdep/dep.c $(B)/samples/libbase.so
	@mkdir -p $(@D)
	$(NDK_CC) $(NDK_FLAGS) $(PACKED) -o $@ $< -L$(B)/samples -lbase

# API 29+ so __thread uses ELF TLS (TLSDESC); every function gets a stack protector.
$(B)/samples/libpatch.so: samples/libpatch/patch.c samples/libpatch/regcheck.S
	@mkdir -p $(@D)
	$(NDK_CC) $(NDK_FLAGS) --target=aarch64-linux-android29 -fstack-protector-all -o $@ $^

$(B)/samples/libshim.so: samples/libshim/shim.c
	@mkdir -p $(@D)
	$(NDK_CC) $(NDK_FLAGS) -o $@ $< -landroid -llog

# NativeActivity sample: native_app_glue from the NDK.
GLUE_DIR := $(NDK_ROOT)/sources/android/native_app_glue
$(B)/samples/libnative.so: samples/libnative/main.c $(GLUE_DIR)/android_native_app_glue.c
	@mkdir -p $(@D)
	$(NDK_CC) $(NDK_FLAGS) -I$(GLUE_DIR) -o $@ $^ -landroid -llog

# C++ with libc++ linked statically into the library.
$(B)/samples/libcxx.so: samples/libcxx/cxx.cpp
	@mkdir -p $(@D)
	$(NDK_CXX) $(NDK_FLAGS) -std=c++17 -static-libstdc++ -o $@ $<

$(B)/samples/libgame.so: samples/libgame/main.c $(GLUE_DIR)/android_native_app_glue.c
	@mkdir -p $(@D)
	$(NDK_CC) $(NDK_FLAGS) -I$(GLUE_DIR) -o $@ $^ -landroid -llog -lEGL -lGLESv2 -laaudio -lm

# $(call build-apk,STAGE,MANIFEST,LIB,[ASSET]): aapt2-compiled manifest + lib/arm64-v8a/LIB
# (deflate) + optional assets/ASSET (stored), so both extraction paths are covered.
define build-apk
	@test -x "$(AAPT2)" || { echo "error: aapt2 not found under $(ANDROID_HOME)/build-tools (set ANDROID_HOME or AAPT2)" >&2; exit 1; }
	@test -f "$(ANDROID_JAR)" || { echo "error: android.jar not found under $(ANDROID_HOME)/platforms (set ANDROID_HOME or ANDROID_JAR)" >&2; exit 1; }
	rm -rf $(1) $@.tmp
	mkdir -p $(1)/lib/arm64-v8a $(1)/assets
	"$(AAPT2)" link -I "$(ANDROID_JAR)" --manifest $(2) -o $@.tmp
	cp $(3) $(1)/lib/arm64-v8a/
	cd $(1) && zip -q -X -9 $(abspath $@.tmp) lib/arm64-v8a/$(notdir $(3))
	$(if $(4),cp $(4) $(1)/assets/ && cd $(1) && zip -q -X -0 $(abspath $@.tmp) assets/$(notdir $(4)))
	mv $@.tmp $@
endef

samples-apk: $(B)/samples/sample.apk $(B)/samples/native.apk $(B)/samples/game.apk ## build the sample APKs (needs aapt2 + android.jar)

# libadd.so + assets/hello.txt, read by test-apk / test-activity.
$(B)/samples/sample.apk: samples/apk/AndroidManifest.xml samples/apk/hello.txt $(B)/samples/libadd.so
	$(call build-apk,$(B)/samples/apk,$<,$(B)/samples/libadd.so,samples/apk/hello.txt)

# NativeActivity sample APK (libnative.so), for running on the device.
$(B)/samples/native.apk: samples/apk-native/AndroidManifest.xml $(B)/samples/libnative.so
	$(call build-apk,$(B)/samples/apk-native,$<,$(B)/samples/libnative.so)

# Sample game APK: libgame.so + assets/game.cfg.
$(B)/samples/game.apk: samples/apk-game/AndroidManifest.xml samples/apk-game/assets/game.cfg $(B)/samples/libgame.so
	$(call build-apk,$(B)/samples/apk-game,$<,$(B)/samples/libgame.so,samples/apk-game/assets/game.cfg)
