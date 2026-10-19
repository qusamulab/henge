# ---- sources ----
# The converter library is everything in convert/ except the CLI (main.c), so it can
# be compiled into other hosts (e.g. the iOS app) on its own.
CONVERT_LIB_SRCS := $(filter-out convert/main.c,$(wildcard convert/*.c))
CONVERT_LIB_OBJS := $(patsubst convert/%.c,$(B)/convert/%.o,$(CONVERT_LIB_SRCS))
APK_LIB_SRCS     := $(filter-out apk/main.c,$(wildcard apk/*.c))
APK_LIB_OBJS     := $(patsubst apk/%.c,$(B)/apk/%.o,$(APK_LIB_SRCS))
# List production sources explicitly: unfinished shim replacements can coexist locally.
RUNTIME_SRCS     := $(addprefix runtime/,activity.c assets.c cpu.c dl.c entry.S jni.c \
                    linker.c linux.c reloc.c shim.c shim_android.c shim_cxx.c \
                    shim_libc.c shim_posix.c symbols.c syscall.c thread.c)
RUNTIME_OBJS     := $(patsubst runtime/%,$(B)/runtime/%.o,$(basename $(RUNTIME_SRCS)))

VM_SRCS := $(wildcard vm/*.c)
VM_OBJS := $(patsubst vm/%.c,$(B)/vm/%.o,$(VM_SRCS)) $(B)/vm/core_dex.o $(B)/vm/android_dex.o

ALL_OBJS := $(CONVERT_LIB_OBJS) $(APK_LIB_OBJS) $(RUNTIME_OBJS) $(VM_OBJS)
