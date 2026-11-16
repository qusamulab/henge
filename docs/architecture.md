# Design

## 1. Goal and assumptions

Run the native code of Android apps (`lib/arm64-v8a/*.so`) on an iPhone at native speed.

iOS constraints:

- Generating and executing code at runtime (RWX / JIT) is forbidden unless a debugger is attached.
- **Signed code can run.** A dylib signed with the same Team ID as the host app can be `dlopen`ed (proven by LiveContainer).

What Android and iPhone have in common:

- Both are ARM64. The machine code in a `.so` runs as-is on the iPhone's CPU.
- What differs is the file format (ELF / Mach-O), the libc (bionic / libSystem), ABI details, and OS APIs.

So henge **does not translate instructions**. It repacks the ELF as Mach-O, signs it on the iPhone, and lays a compatibility layer for the Android runtime environment (bionic, Android APIs) underneath.

### Comparison with other approaches

![Approach comparison](images/comparison.png)

| Approach | Example | Speed | Resilience to iOS updates | Drawback |
| --- | --- | --- | --- | --- |
| Map ELF directly with JIT | Husk | ◎ | × | Depends on debugger-enabled JIT. Over if Apple closes it |
| Interpreter | LiquidAPK | △ | ◎ | Slow (about 80–90M instructions/s) |
| CPU emulation | Husk (QEMU) | × | ○ | Very slow |
| **AOT conversion + signing (henge)** | — | ◎ | ◎ | Weak against code generated or downloaded at runtime (§7) |

henge also contains an interpreter (`henge-vm`, §8), but only for Dalvik bytecode. ARM64 machine code is never interpreted.

## 2. Overview

![Pipeline](images/pipeline.png)

```
┌──────────────────────── henge host app (installed once via SideStore etc.) ──────────────────────────────┐
│                                                                                                         │
│  ① Import          ② Convert             ③ Sign                 ④ Load                ⑤ Run              │
│  Receive APK  →  ELF → Mach-O dylib  →  Sign with own cert  →  dlopen + mini linker → Start NativeActivity│
│  (share sheet)     (henge-convert)        (on-device signing)    (relocation, init)    (UIKit etc. via shims)│
│                                                                                                         │
│  ┌──────────────────────────────── runtime ────────────────────────────────┐                            │
│  │ libhenge-bionic   libc / libm / libdl / liblog compatibility            │                            │
│  │ libhenge-android  NativeActivity / AAsset / AInput / ANativeWindow / JNI │                            │
│  │ EGL / GLES        EGL on EAGL; generated GLES ABI wrappers (ANGLE later)  │                            │
│  │ Audio             AAudio → AudioUnit (OpenSL ES not yet)                 │                            │
│  └─────────────────────────────────────────────────────────────────────────┘                            │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Components

| Component | Language | Role |
| --- | --- | --- |
| `henge-convert` | C | Converts ELF `.so` files into Mach-O dylibs. Also patches code (svc, TLS register). Runs both on the iPhone and on macOS |
| Signer | C/Swift | Built-in ad hoc / CMS signer (`convert/sign.c`, `cms.c`, `signer_sec.c`); signs converted dylibs with an imported developer identity matching the host app |
| Mini linker | C | After `dlopen`, reads the ELF dynamic info, resolves relocations, and calls initializers |
| bionic shim (`runtime/shim*.c`) | C + asm | A table of bionic function names bridged to Darwin's libSystem. Absorbs ABI differences ([abi-differences.md](../research/abi-differences.md)). Compiled into the host app, not a separate library |
| NDK shim (`runtime/shim_android.c`, `activity.c`, `assets.c`, `app/HengeHost/Platform/`) | C/ObjC | Implements NDK APIs (`libandroid.so`, EGL, AAudio) with UIKit / OpenGL ES / AudioUnit |
| `henge-vm` (`vm/`) | C + Java | Dalvik bytecode interpreter with henge's own `java.*` and `android.*` classes (§8). Connected to native libraries through JNI and to the iOS host; Android class-library coverage remains limited |
| Host app | Swift | APK management UI, runs conversion and signing, hosts the game view |

## 3. Conversion (henge-convert)

Details in [m1-elf-to-macho.md](m1-elf-to-macho.md). Key points:

1. Repack ELF `PT_LOAD` segments into Mach-O `__TEXT` (R / RX) and `__DATA` (RW) **while preserving their relative positions**. Android `.so` files are position-independent and use page-relative references via `adrp`, so as long as relative positions are preserved they work wherever they are loaded.
2. Shift everything by the header area (at least 16 KiB, enlarged for patch islands and missing-function stubs). A shift that is a multiple of 4 KiB does not change `adrp` results.
3. Code patches (done before signing, so the code can be freely rewritten):
   - Linux `svc #0` → `bl` to the shim's syscall stub
   - `mrs xN, tpidr_el0` → replace with TLS access via the shim if needed (depends on the verification results)
4. Keep the ELF dynamic info (`.dynamic`, `.dynsym`, relocation tables) as-is. dyld does not touch it; the mini linker reads it.
5. Register no initializers in the Mach-O. ELF `DT_INIT` / `DT_INIT_ARRAY` are called by the mini linker after relocation.

**Conditions that cannot be converted (rejected in M1)**

- Code and data share the same 16 KiB page (older `.so` files linked with 4 KiB alignment)
- `DT_TEXTREL` is present (forbidden on Android API 23+, so practically nonexistent)
- APKs that contain only 32-bit ARM

Since November 2025, Google Play has required 16 KiB page support for new apps and updates targeting Android 15 or later. The newer the APK, the more likely it can be converted.

## 4. Signing

- The user exports their Apple Development certificate and private key as a `.p12` and imports it in the host app's **Signing** tab. The `.p12` and its password are stored in the device's Keychain.
- Converted dylibs are cached under `Library/Caches/henge/artifacts-v4/` and signed there with `convert/sign.c` / `cms.c` / `signer_sec.c`.
- The host app and the dylibs share a Team ID, so they pass library validation. An ad hoc signature is rejected on a device (V3).
- macOS and the iOS Simulator accept the built-in ad hoc signature.

Current status and remaining constraints:

- **Free Apple ID certificates** (the certificate SideStore / AltStore used at install time, as LiveContainer does). Only a paid developer identity has been tried (V3). Until this is verified, henge needs a paid account, and users without a Mac cannot get a `.p12`.
- **Implemented cache.** Import prepares unsigned libraries keyed by APK SHA-256, converter version and target platform. A separate signed cache is keyed by certificate SHA-256 and signing version, with a 24-hour maximum reuse age. Cache hits verify file hashes. This age limit is a refresh policy; it does not validate certificate or provisioning expiration. App data stays in `Application Support/henge/data/<package>/`, outside the cache.
- **Implemented file pipeline.** APK extraction, conversion and signing have file-backed mmap APIs with temporary-file publication. Errors preserve existing outputs. Signing flushes and invalidates shared pages before loading, as required by Darwin code-signing validation. The buffer APIs remain available. Peak resident memory for a 100 MiB engine has not yet been measured.
- **Key isolation.** Converted code runs in the host process with the host's entitlements, so it can read the `.p12` and password from the Keychain. Only process separation (§5.1) prevents this; until then, "only run APKs you trust" is the only protection.

## 5. Loading and execution

1. **Resolve NEEDED**: Look at the ELF `DT_NEEDED` entries and load dependencies first.
   - Bundled in the APK (`libc++_shared.so`, `libmain.so`, etc.) → converted dylibs
   - System libraries (`libc.so`, `libm.so`, `libdl.so`, `liblog.so`, `libandroid.so`, `libEGL.so`, `libGLESv2.so`, `libGLESv3.so`, `libOpenSLES.so`, `libaaudio.so`, `libz.so`) → henge shims
2. **dlopen**: `dlopen` the converted dylib. dyld only maps code and data and verifies the signature; it does not interpret the ELF contents.
3. **Find the base address**: Look up our dylib by path in dyld's image list (`_dyld_get_image_header`).
4. **Relocation**: The mini linker rewrites the GOT and pointers in `__DATA`. Relocations use the global group followed by the image and its breadth-first `DT_NEEDED` tree (§5.2).
5. **Initialization**: Call `DT_INIT`, then `DT_INIT_ARRAY`.
6. **Launch**: Call `ANativeActivity_onCreate`, or start the manifest's Java launcher Activity on henge-vm and let its `System.loadLibrary` / JNI calls reach native code.

If the app calls `dlopen("libfoo.so")` at runtime, the shim's `dlopen` returns the converted dylib using the same steps.

### 5.1 Process model

Converted images are never unloaded: dyld mappings, native globals, initialized libraries and guest pthreads can survive Stop. The host enforces **one process runs one guest, once**, claiming the process immediately before its first guest load, including when that load later fails. A second run shows **Restart Required**. Diagnostics also claim the process; the M1 test no longer runs automatically on ordinary app launch.

Stop delivers lifecycle callbacks and ends the host frame loop. It cannot cancel arbitrary guest threads. A VM that has loaded a JNI library, its DEX backing buffers, and its host/window context are retained until process exit because native code can keep JavaVM pointers and attach again later. Pure Java test VMs can still be destroyed and recreated.

The user currently restarts henge from the app switcher. Automated relaunch and signing-key isolation remain open decisions:

| Option | How | Pros | Cons |
| --- | --- | --- | --- |
| Relaunch the host | Record the pending launch, exit, start again into the guest (LiveContainer's original model) | Simple; full app memory limit | The library UI and the guest cannot coexist; visible restart |
| Guest in an app extension | The host stays up; an extension process loads and runs the guest | Fresh process per launch; the extension can be kept out of the Keychain group that holds the signing identity (§4) | Lower memory limit for extensions (to be measured); rendering and input must cross the process boundary |

The single-use guard is implemented and concurrency-tested. It does not provide process or Keychain isolation.

### 5.2 Symbol lookup

Relocations search `DF_1_GLOBAL` / `RTLD_GLOBAL` images first, then the current image and its `DT_NEEDED` closure breadth-first. Shim-backed system libraries occupy their positions in that tree. Hidden/internal exports are excluded; local/protected definitions bind within their image. `dlsym(handle)` shares the traversal, with cycle deduplication and no fixed dependency-count cutoff.

This is not a complete bionic linker: actual `DT_NEEDED` load cycles are rejected, symbol versions/namespaces are not modeled, and `RTLD_DEFAULT` / `RTLD_NEXT` retain the older process-wide shim-first lookup. The scope regression covers unrelated local images, global precedence, BFS, system dependencies and 600 dependency entries.

### 5.3 Undefined symbols and shim generation

Before signing, the converter emits a static named abort stub for every strong `STT_FUNC` import in patch-island version 2. The linker uses a stub only if real symbol resolution fails. An unused unsupported function therefore does not prevent loading. Calling it logs its name, adds it to the crash record and aborts. Weak imports remain zero. Unresolved data, TLS and unknown symbol types still fail loading; treating these as callable would corrupt memory.

The APK detail screen lists imports and unresolved symbols across each dependency closure, distinguishes callable stubs from load blockers, and reports conversion errors and missing libraries. This static report cannot predict reflection, symbol versions or actual runtime paths.

`scripts/libc-api.json` is a reviewed LP64 classification of the supported bionic names: direct ABI-compatible entry, generated errno wrapper, generated error-code/math wrapper, or handwritten adapter. `scripts/generate-libc.py` emits `runtime/libc_generated.c`; it does not turn unreviewed libc symbols into arbitrary Darwin pointers. This is a curated subset, not full bionic coverage.

### 5.4 Crash reporting

The host installs a recorder for SIGSEGV/BUS/ILL/FPE/ABRT/TRAP before running a guest. The handler writes a fixed binary record with PC/LR/SP/FP, x0–x28, fault address, image/ELF base and the last missing-function name to a preopened file, using immutable image snapshots. It restores the default action and re-raises the signal so the OS can produce its own report. On the next launch, the APK screen displays and shares the consumed record.

Subprocess tests exercise a trap inside converted code and a missing-function abort. SIGKILL (including code-signature enforcement), out-of-memory termination and stack exhaustion without a usable signal stack cannot be recorded this way. Guest `sigaction` / Linux signal-context conversion remain unsupported.

## 6. Android compatibility layer

### bionic (libc)

- Export functions under bionic names; implement them by calling Darwin's libSystem.
- Convert struct differences (`stat`, `dirent`, `sigaction`, `pthread_mutex_t`, etc.) and constant differences (`errno`, `open` / `mmap` flags, signal numbers).
- **ABI differences are the biggest pitfall.** Variadic arguments, `va_list`, `long double`, etc. are completely different from Darwin. See [abi-differences.md](../research/abi-differences.md).
- **`errno`.** Android reads Linux values through henge's thread-local `__errno()`. Generated wrappers cover allocation, string-number conversion, stdio, time and libm: they isolate Darwin errno, translate newly reported errors and preserve guest errno on successful calls. Math wrappers also translate floating-point domain/range exceptions because Darwin libm does not consistently set errno. Struct/constant adapters retain handwritten handling.

### NDK API

| Android | iOS implementation |
| --- | --- |
| `ANativeActivity` and lifecycle callbacks | `UIViewController` lifecycle |
| `ANativeWindow` | `UIView` backed by a `CAEAGLLayer` |
| EGL | Implemented on EAGL (`app/HengeHost/Platform/EGLShim.m`); framebuffer 0 is mapped to the layer's framebuffer |
| GLES 2 and 3.0 | Generated ABI boundary to iOS OpenGL ES (see below) |
| Vulkan | Not done. MoltenVK |
| `AInputQueue` / `AInputEvent` | `UITouch` (done); key events and `GCController` not done |
| `AAssetManager` | Read `assets/` from the APK |
| AAudio | AudioUnit (`AudioShim.m`) |
| OpenSL ES | Not done |
| `__android_log_print` | `os_log` |
| `AConfiguration`, display info | `UIScreen`, `UITraitCollection` |

### GLES boundary

The default backend is iOS OpenGL ES, with EGL implemented on EAGL. `scripts/gl-api.json` pins the GLES 2.0 / 3.0 core signatures extracted from Khronos `gl.xml`; `scripts/generate-gl.py` generates typed wrappers. GP parameters use Android-sized incoming slots and are narrowed before the Darwin backend call; floating-point parameters retain their types. This handles the packed stack arguments of `glBlitFramebuffer`, `glTexSubImage3D` and narrow `GLboolean` arguments. A converted NDK test verifies these paths on macOS with a deterministic backend.

`henge_gl_set_backend` selects a resolver before guest execution, allowing a future ANGLE backend. Unsupported extensions and absent backend functions return no pointer, including through `eglGetProcAddress`; there is no unreviewed raw `dlsym` fallback. Apple's backend still stops at ES 3.0. ANGLE, ES 3.1+, Vulkan and arbitrary GLES extensions are not implemented.

### JNI

Java activities use the JNI bridge in `vm/jni.c`, described below. The standalone `runtime/jni.c` fallback still supplies a minimal probing environment for pure NativeActivity apps that do not start henge-vm. That fallback does not provide Java objects.

## 7. Code added at runtime

When `henge_load` or Android `dlopen` encounters an ELF file, it calls the host's `henge_set_elf_prepare` hook. The iOS host hashes the ELF, converts it through the file-backed converter, signs it using the session's retained identity, and returns a persistent cache path before normal loading. Bare library names can also find unconverted `.so` files along the configured library path. Hosts without a hook receive an explicit error.

`make test-files` converts, signs and immediately loads a previously unconverted ELF inside one macOS process. Equivalent loading after native guest execution begins is also covered there. iPhone acceptance of libraries newly signed during guest execution remains V4 and requires a device.

Self-unpacking packers and DRM that generates code in memory are out of scope.

## 8. Java / Kotlin: henge-vm

The original plan was to port ART and `dex2oat` and AOT-compile dex on the iPhone. What was built instead is `henge-vm` (`vm/`): a Dalvik bytecode interpreter in C with henge's own `java.*` core classes (`vm/core`) and a small `android.*` subset (`vm/android`: `Activity`, `GLSurfaceView`, `GLES20`, `AudioTrack`, `Handler`, ...). It runs Java and Kotlin (kotlinc + R8) tests and a Kotlin sample game (`samples/kcatch`) on macOS. It has no JIT and no AOT, object references are 32-bit handles into a non-moving table, and one global lock serializes all threads.

ART is dropped: its size, its reliance on SIGSEGV for null and stack checks, and the unverified `dex2oat` questions (V7) are avoided, at the price of interpreter speed and of writing the class library.

### 8.1 Role

In the games henge targets, Java is glue: the `Activity`, lifecycle, input forwarding, asset and preference access. The frame loop is native. So:

- **Native code runs converted, at full speed. The APK's own dex runs on henge-vm.** The two meet at JNI.
- henge does not reimplement each engine's Java side in C. `SDLActivity.java` or `GodotActivity` from the APK run as they are.
- Scope of `android.*`: what engine glue uses (`Activity`, `SurfaceView` / `GLSurfaceView`, input events, `AssetManager`, `SharedPreferences`, `AudioTrack` / `SoundPool`, `Handler` / `Looper`, `Build`, display metrics). The `View` / layout / resource system for ordinary apps is a non-goal.

### 8.2 The JNI bridge

`System.loadLibrary` / `System.load` call `henge_load`, then `JNI_OnLoad`. JNI provides method/field lookup, object creation, virtual/nonvirtual/static calls, primitive/object arrays, modified UTF-8 / UTF-16 strings, pending exceptions and native registration. Local/global references are generation-tagged opaque tokens; strong refs and acquired array/string contents are pinned as GC roots, and weak refs are cleared before handle reuse.

Android variadic calls use generated static assembly entries that spill argument registers into an AAPCS64 `va_list` for the V implementations. VM-to-native calls use one static assembly bridge that packs GP/FP registers and 8-byte stack slots from the method descriptor. No executable memory or JIT is allocated. The VM lock is released during native calls and blocking sleeps; attached pthreads get their own Thread and JNIEnv and can call back into Java. Failed JNI_OnLoad is remembered and its native registrations are rolled back.

`make test-jni` covers registration, Java_* lookup, constructors/fields, strings, array leases/regions, GC, A/V/variadic calls, 10 GP plus 10 FP parameters in both directions and a native-thread Java callback. `make test-vm-jni-host` starts a real APK launcher Activity, loads its converted native library, calls JNI and finishes its lifecycle on macOS. The iOS host routes Java launchers to this same host, forwards touch/resize and uses the EAGL surface for GLSurfaceView. The iOS build is validated; device execution of this new route remains unverified.

Remaining JNI/class-runtime limitations: reflection conversion, DefineClass, monitors and direct ByteBuffer JNI operations raise UnsupportedOperationException; Java Thread.start still runs synchronously, Object wait/notify and interpreter monitor semantics are incomplete, and arbitrary engine SurfaceView/platform-service glue is not implemented. Attached native pthreads are supported; full Android/Java thread scheduling is not. An SDL2/Godot APK is still needed to drive the next compatibility work.

### 8.3 Class library (open decision)

`vm/core` is written from scratch (about 130 classes). R8-processed Kotlin and engine glue reach into `java.util.concurrent`, reflection, `String.format`, `java.io` / `java.nio`, and more. Either keep growing it, driven by what target APKs reference, or adopt AOSP libcore (complete, but tied to ART internals and to native libraries, and GPLv2 with the Classpath exception). For now the class library remains henge's own implementation. `python3 scripts/check-dex.py app.apk` lists missing class, method and field references across all root `classes*.dex` entries, the APK definitions and both bundled class libraries, without executing guest code. It resolves inherited members, skips VM-ignored Dalvik metadata annotations, supports `--json`, and exits 1 for missing references or 2 for malformed input. Reflection and service behavior still require runtime testing.

## 9. Constraints and risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Apple blocks `dlopen` of self-signed dylibs | The whole approach fails | Watch projects using the same approach, such as LiveContainer. Worst case, switch to an interpreter |
| The approach needs a paid developer identity | Users without a paid account, or without a Mac to export a `.p12`, cannot use henge | Verify the free Apple ID / SideStore certificate path (V3) before M4 |
| Guest code can read the signing identity | A malicious APK steals the user's developer certificate | Process separation (§5.1). Until then, only run trusted APKs |
| Apple removes OpenGL ES | No rendering | Keep the GL boundary generated so the backend can move to ANGLE (§6) |
| Interpreter speed for Java | Games with real logic in Java or Kotlin are slow | Accepted: the target is games whose Java side is glue (§8.1) |
| Memory limit (jetsam) | Large games crash | Check whether `increased-memory-limit` is available to free accounts (to be verified) |
| Missed ABI differences | Hard-to-diagnose crashes | List the functions that cross the boundary and write tests ([abi-differences.md](../research/abi-differences.md)) |
| Google Play Services dependency | Login and billing do not work | Out of scope. Use a fake GMS that reports "not signed in" so the app at least starts |
| Licenses | ANGLE (BSD) and MoltenVK (Apache-2.0) are fine. Check when bringing in a signer or ART | Check every time a component is added |
