# ---- converter + runtime (host) ----
$(B)/convert/%.o: convert/%.c
	@mkdir -p $(@D)
	$(COMPILE.c) -c $< -o $@

$(B)/libhenge-convert.a: $(CONVERT_LIB_OBJS) mk/sources.mk
	rm -f $@
	$(AR) rcs $@ $(CONVERT_LIB_OBJS)

# The CLI links the runtime only for henge_shim_provides (--check reports).
$(B)/henge-convert: convert/main.c $(B)/libhenge-convert.a $(B)/libhenge-runtime.a $(B)/libhenge-apk.a
	$(COMPILE.c) $< $(B)/libhenge-convert.a $(B)/libhenge-runtime.a $(B)/libhenge-apk.a -lz -framework Security -framework CoreFoundation $(LDFLAGS) $(LDLIBS) -o $@

$(B)/runtime/%.o: runtime/%.c
	@mkdir -p $(@D)
	$(COMPILE.c) -c $< -o $@

$(B)/runtime/%.o: runtime/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $@.d -MT $@ -c $< -o $@

$(B)/libhenge-runtime.a: $(RUNTIME_OBJS) mk/sources.mk
	rm -f $@
	$(AR) rcs $@ $(RUNTIME_OBJS)

# ---- henge-vm: a Dalvik bytecode interpreter (vm/) with its own core class library (vm/core) ----

$(B)/vm/%.o: vm/%.c
	@mkdir -p $(@D)
	$(COMPILE.c) -Ivm -c $< -o $@

$(B)/vm/core_dex.c: vm/core/henge-core.dex scripts/embed-dex.py
	@mkdir -p $(@D)
	python3 scripts/embed-dex.py $< $@ vm_core_dex

$(B)/vm/core_dex.o: $(B)/vm/core_dex.c
	$(COMPILE.c) -c $< -o $@

$(B)/vm/android_dex.c: vm/android/henge-android.dex scripts/embed-dex.py
	@mkdir -p $(@D)
	python3 scripts/embed-dex.py $< $@ vm_android_dex

$(B)/vm/android_dex.o: $(B)/vm/android_dex.c
	$(COMPILE.c) -c $< -o $@

$(B)/libhenge-vm.a: $(VM_OBJS) mk/sources.mk
	rm -f $@
	$(AR) rcs $@ $(VM_OBJS)

# ---- APK reader (apk/, zlib) ----
$(B)/apk/%.o: apk/%.c
	@mkdir -p $(@D)
	$(COMPILE.c) -c $< -o $@

$(B)/libhenge-apk.a: $(APK_LIB_OBJS) mk/sources.mk
	rm -f $@
	$(AR) rcs $@ $(APK_LIB_OBJS)

$(B)/henge-apk: apk/main.c $(B)/libhenge-apk.a
	$(COMPILE.c) $< $(B)/libhenge-apk.a -lz $(LDFLAGS) $(LDLIBS) -o $@

$(ALL_OBJS): mk/config.mk mk/libraries.mk mk/sources.mk
$(B)/henge-convert $(B)/henge-apk: mk/config.mk mk/libraries.mk
