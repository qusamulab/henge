# ---- tests (macOS) ----
TESTS := test_layout test_m1 test_patch test_dl test_shim test_cxx test_activity test_apk test_apk_index test_vm test_vm_lifecycle test_library
tests: $(addprefix $(B)/tests/,$(TESTS))

# Most tests drive converted code through the runtime (+ the APK reader for assets).
$(B)/tests/test_%: tests/test_%.c $(B)/libhenge-runtime.a $(B)/libhenge-apk.a
	@mkdir -p $(@D)
	$(COMPILE.c) $< $(B)/libhenge-runtime.a $(B)/libhenge-apk.a -lz $(LDFLAGS) $(LDLIBS) -o $@

$(B)/tests/test_vm $(B)/tests/test_vm_lifecycle: $(B)/tests/%: tests/%.c $(B)/libhenge-vm.a $(B)/libhenge-runtime.a $(B)/libhenge-apk.a
	@mkdir -p $(@D)
	$(COMPILE.c) -I. $< $(B)/libhenge-vm.a $(B)/libhenge-runtime.a $(B)/libhenge-apk.a -lz -lm $(LDFLAGS) $(LDLIBS) -o $@

$(B)/tests/test_layout: tests/test_layout.c
	@mkdir -p $(@D)
	$(COMPILE.c) $< $(LDFLAGS) $(LDLIBS) -o $@

$(B)/tests/test_apk $(B)/tests/test_apk_index: $(B)/tests/%: tests/%.c $(B)/libhenge-apk.a
	@mkdir -p $(@D)
	$(COMPILE.c) -Iapk $< $(B)/libhenge-apk.a -lz $(LDFLAGS) $(LDLIBS) -o $@

LIBRARY_SWIFT_SRCS := app/HengeHost/Core/APKMetadata.swift \
    app/HengeHost/Features/Library/APKStorage.swift app/HengeHost/Features/Library/APKLibrary.swift

$(B)/tests/test_library: tests/test_library.swift $(LIBRARY_SWIFT_SRCS) $(B)/libhenge-apk.a include/henge/apk.h
	@mkdir -p $(@D)
	xcrun swiftc -parse-as-library -import-objc-header include/henge/apk.h \
	  tests/test_library.swift $(LIBRARY_SWIFT_SRCS) $(B)/libhenge-apk.a -lz -o $@

# Converted dylibs for the macOS tests, ad-hoc signed by henge-convert's built-in
# signer and checked with Apple's codesign. Always reconverted (FORCE) so CONVERT_FLAGS
# changes take effect.
.PHONY: FORCE
$(B)/tests/%.dylib: $(B)/samples/%.so $(B)/henge-convert FORCE
	@mkdir -p $(@D)
	$(B)/henge-convert --platform macos --sign $(CONVERT_FLAGS) -m $(B)/tests/$*.manifest.json -o $@ $<
	codesign -v $@

test-patch: $(B)/tests/test_patch $(B)/tests/libpatch.dylib ## run patched svc / tpidr_el0 / TLS / IFUNC code on macOS
	$(B)/tests/test_patch $(B)/tests/libpatch.dylib

# Converted libdep + libbase in their own directory (the test's library path).
.PHONY: $(B)/tests/dl
$(B)/tests/dl: $(B)/samples/libdep.so $(B)/samples/libbase.so $(B)/henge-convert
	@mkdir -p $@
	for l in libdep libbase; do \
	  $(B)/henge-convert --platform macos --sign $(CONVERT_FLAGS) -o $@/$$l.dylib $(B)/samples/$$l.so && \
	  codesign -v $@/$$l.dylib || exit 1; \
	done

test-dl: $(B)/tests/test_dl $(B)/tests/dl ## DT_NEEDED loading, APS2 and libdl on macOS
	$(B)/tests/test_dl $(B)/tests/dl

test-shim: $(B)/tests/test_shim $(B)/tests/libshim.dylib ## pthread / errno / fd / ALooper / AConfiguration shims on macOS
	$(B)/tests/test_shim $(B)/tests/libshim.dylib

test-activity: $(B)/tests/test_activity $(B)/tests/libnative.dylib $(B)/samples/sample.apk ## run a native_app_glue NativeActivity through the host on macOS
	$(B)/tests/test_activity $(B)/tests/libnative.dylib $(B)/samples/sample.apk

# Needs a code signing identity in the keychain (Apple Development / Developer ID); skipped without one.
SIGN_IDENTITY ?= Apple Development
test-sign: $(B)/henge-convert $(B)/samples/libadd.so ## sign with a keychain identity and check it with codesign (skipped without one)
	@if security find-identity -v -p codesigning | grep -q "$(SIGN_IDENTITY)"; then \
	  mkdir -p $(B)/tests && \
	  $(B)/henge-convert --platform macos --sign-identity "$(SIGN_IDENTITY)" -o $(B)/tests/libadd.signed.dylib $(B)/samples/libadd.so && \
	  codesign --verify --strict -vvv $(B)/tests/libadd.signed.dylib && \
	  codesign -dvvv $(B)/tests/libadd.signed.dylib 2>&1 | grep -E "TeamIdentifier|Authority" && \
	  echo "PASS: test_sign"; \
	else echo "SKIP: test_sign (no '$(SIGN_IDENTITY)' identity in the keychain)"; fi

test-cxx: $(B)/tests/test_cxx $(B)/tests/libcxx.dylib ## C++ (libc++, exceptions, RTTI, threads) on macOS
	$(B)/tests/test_cxx $(B)/tests/libcxx.dylib

test-vm: $(B)/tests/test_vm ## run Java test code on the henge-vm bytecode interpreter
	$(B)/tests/test_vm vm/tests/vmtest.dex 'LVmTest;'
	$(B)/tests/test_vm vm/tests/kttest.dex 'Lkt/KtTestKt;'
	$(B)/tests/test_vm vm/tests/vmtest.dex 'LAndroidTest;'

.PHONY: test-vm-lifecycle
test-vm-lifecycle: $(B)/tests/test_vm_lifecycle $(B)/samples/sample.apk ## check VM restart, GC accounting and failed-start cleanup
	$(B)/tests/test_vm_lifecycle $(B)/samples/sample.apk

test-layout: $(B)/tests/test_layout $(B)/tests/libadd.dylib ## check the converted Mach-O layout
	$(B)/tests/test_layout $(B)/samples/libadd.so $(B)/tests/libadd.dylib

test-m1: $(B)/tests/test_m1 $(B)/tests/libadd.dylib ## dlopen + henge_link + call libadd on macOS
	$(B)/tests/test_m1 $(B)/tests/libadd.dylib

test-apk: $(B)/tests/test_apk $(B)/tests/test_apk_index $(B)/samples/sample.apk $(B)/henge-apk ## read the sample APK (zip + manifest) and verify indexed lookup
	$(B)/tests/test_apk $(B)/samples/sample.apk $(B)/samples/libadd.so samples/apk/hello.txt
	$(B)/tests/test_apk_index
	$(B)/henge-apk info $(B)/samples/sample.apk

test-library: $(B)/tests/test_library $(B)/samples/sample.apk ## test APK inspection, imports, search and sorting on macOS
	$(B)/tests/test_library $(B)/samples/sample.apk

test: test-layout test-m1 test-patch test-dl test-shim test-cxx test-vm test-vm-lifecycle test-activity test-sign test-apk test-library ## run all macOS tests
	@echo "ALL TESTS PASSED"


$(addprefix $(B)/tests/,$(TESTS)): mk/config.mk mk/tests.mk
