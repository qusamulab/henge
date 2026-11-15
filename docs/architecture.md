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
│  │ EGL / GLES        EGL on EAGL; GLES passed through to iOS (ANGLE later)  │                            │
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
| `henge-vm` (`vm/`) | C + Java | Dalvik bytecode interpreter with henge's own `java.*` and `android.*` classes (§8). Not yet connected to the native runtime or the iOS host |
| Host app | Swift | APK management UI, runs conversion and signing, hosts the game view |

## 3. Conversion (henge-convert)

Details in [m1-elf-to-macho.md](m1-elf-to-macho.md). Key points:

1. Repack ELF `PT_LOAD` segments into Mach-O `__TEXT` (R / RX) and `__DATA` (RW) **while preserving their relative positions**. Android `.so` files are position-independent and use page-relative references via `adrp`, so as long as relative positions are preserved they work wherever they are loaded.
2. Shift everything by the size of the Mach-O header (16 KiB). A shift that is a multiple of 4 KiB does not change `adrp` results.
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
- Converted dylibs are written to the app container (`Library/Application Support/henge/run/<package>/`) and signed there with `convert/sign.c` / `cms.c` / `signer_sec.c`.
- The host app and the dylibs share a Team ID, so they pass library validation. An ad hoc signature is rejected on a device (V3).
- macOS and the iOS Simulator accept the built-in ad hoc signature.

Not done yet:

- **Free Apple ID certificates** (the certificate SideStore / AltStore used at install time, as LiveContainer does). Only a paid developer identity has been tried (V3). Until this is verified, henge needs a paid account, and users without a Mac cannot get a `.p12`.
- **Conversion cache.** `ConvertRunner` deletes the run directory and converts and signs every library on every launch. The target is: convert at import, key the output by (APK hash, converter version, certificate), and re-sign only when the certificate changes (free certificates expire after 7 days).
- **Memory.** `henge_convert` and the signer each take and return whole buffers, so a library is in memory three times (ELF, Mach-O, signed Mach-O). A 100 MiB engine library needs a file-backed (mmap) pipeline.
- **Key isolation.** Converted code runs in the host process with the host's entitlements, so it can read the `.p12` and password from the Keychain. Only process separation (§5.1) prevents this; until then, "only run APKs you trust" is the only protection.

## 5. Loading and execution

1. **Resolve NEEDED**: Look at the ELF `DT_NEEDED` entries and load dependencies first.
   - Bundled in the APK (`libc++_shared.so`, `libmain.so`, etc.) → converted dylibs
   - System libraries (`libc.so`, `libm.so`, `libdl.so`, `liblog.so`, `libandroid.so`, `libEGL.so`, `libGLESv2.so`, `libGLESv3.so`, `libOpenSLES.so`, `libaaudio.so`, `libz.so`) → henge shims
2. **dlopen**: `dlopen` the converted dylib. dyld only maps code and data and verifies the signature; it does not interpret the ELF contents.
3. **Find the base address**: Look up our dylib by path in dyld's image list (`_dyld_get_image_header`).
4. **Relocation**: The mini linker rewrites the GOT and pointers in `__DATA`. Symbols are looked up in the order "shims → every linked image in load order" (§5.2).
5. **Initialization**: Call `DT_INIT`, then `DT_INIT_ARRAY`.
6. **Launch**: Call the entry point such as `ANativeActivity_onCreate` (NativeActivity) or `SDL_main`.

If the app calls `dlopen("libfoo.so")` at runtime, the shim's `dlopen` returns the converted dylib using the same steps.

### 5.1 Process model (open decision)

Converted images are never unloaded: dyld keeps the mapping, `hr_images` is process-wide, and initializers have run. The current host allows a second **Convert & Run** in the same process, which is wrong in two ways:

- A second APK that has a library with the same soname (`libmain.so`) gets the first APK's image back (`find_linked` in `runtime/linker.c`).
- Running the same APK again calls `ANativeActivity_onCreate` on globals left over from the previous run.

Proposed rule: **one process runs one guest, once.** Two ways to get there:

| Option | How | Pros | Cons |
| --- | --- | --- | --- |
| Relaunch the host | Record the pending launch, exit, start again into the guest (LiveContainer's original model) | Simple; full app memory limit | The library UI and the guest cannot coexist; visible restart |
| Guest in an app extension | The host stays up; an extension process loads and runs the guest | Fresh process per launch; the extension can be kept out of the Keychain group that holds the signing identity (§4) | Lower memory limit for extensions (to be measured); rendering and input must cross the process boundary |

Until this is decided, the host should at least refuse a second run in the same process.

### 5.2 Symbol lookup (gap)

`hr_resolve_global` searches the shim first, then every linked image in load order. bionic searches the global group, then **the library itself, then its `DT_NEEDED` tree breadth-first**. The difference matters when two libraries export the same name, typically two libraries that each link libc++ statically and export `operator new` or `__cxa_*`: a library can bind to another library's copy. Target: the bionic order, with the shim taking the place of `libc.so` and the other system libraries in the `DT_NEEDED` tree. `dlsym(handle)` already walks the tree breadth-first (`runtime/dl.c`).

### 5.3 Undefined symbols (gap)

Any undefined non-weak symbol fails the whole load. The shim exports about 200 names; a real engine library imports several hundred to a thousand, so finding the missing ones costs one failed launch each. Target:

- The converter already knows every import. For names the shim lacks, emit one stub per name into the patch island before signing; a stub reports its name and aborts when called. The library then loads, and only the functions actually reached need to exist.
- Generate the pass-through part of the shim table from bionic's symbol lists, classified by ABI ([abi-differences.md](../research/abi-differences.md)): identical signature and no `errno` → direct pointer; sets `errno` → generated wrapper; everything else → handwritten.
- `henge-convert` already prints the missing imports; show the same report in the host app's APK detail screen.

### 5.4 Crash reporting (gap)

A fault in guest code kills the host with no information. The host needs a signal handler that records the faulting image and offset (`hr_image_for_addr`) and the registers, so that the next launch can show "crashed in `libgame.so` + 0x1234". Android code's own `sigaction` is not shimmed yet.

## 6. Android compatibility layer

### bionic (libc)

- Export functions under bionic names; implement them by calling Darwin's libSystem.
- Convert struct differences (`stat`, `dirent`, `sigaction`, `pthread_mutex_t`, etc.) and constant differences (`errno`, `open` / `mmap` flags, signal numbers).
- **ABI differences are the biggest pitfall.** Variadic arguments, `va_list`, `long double`, etc. are completely different from Darwin. See [abi-differences.md](../research/abi-differences.md).
- **`errno` is not uniform yet (gap).** Android code reads `errno` through `__errno()`, which returns a henge thread-local holding Linux values. Wrapped functions (`read`, `write`, ...) translate into it; functions exported as direct pointers to libSystem (`strtoul`, `fwrite`, ...) set Darwin's `errno` instead, which Android code never sees. Every function that can set `errno` needs a wrapper (§5.3).

### NDK API

| Android | iOS implementation |
| --- | --- |
| `ANativeActivity` and lifecycle callbacks | `UIViewController` lifecycle |
| `ANativeWindow` | `UIView` backed by a `CAEAGLLayer` |
| EGL | Implemented on EAGL (`app/HengeHost/Platform/EGLShim.m`); framebuffer 0 is mapped to the layer's framebuffer |
| GLES 2 and 3.0 | Passed through to iOS's OpenGL ES (see below) |
| Vulkan | Not done. MoltenVK |
| `AInputQueue` / `AInputEvent` | `UITouch` (done); key events and `GCController` not done |
| `AAssetManager` | Read `assets/` from the APK |
| AAudio | AudioUnit (`AudioShim.m`) |
| OpenSL ES | Not done |
| `__android_log_print` | `os_log` |
| `AConfiguration`, display info | `UIScreen`, `UITraitCollection` |

### GLES boundary

The original plan was ANGLE on Metal. iOS still ships OpenGL ES, so M3 took the shorter path: any `gl[A-Z]*` name the shim does not define resolves with `dlsym` to the system framework. Two consequences:

- **Ceiling.** Apple's OpenGL ES stops at 3.0 and is deprecated. A game that needs ES 3.1+ (compute shaders) or an extension Apple lacks needs ANGLE; if Apple removes OpenGL ES, everything does.
- **ABI holes in the pass-through (gap).** A direct pointer is only correct when both ABIs pass the arguments the same way. They differ for the 9th and later arguments (8-byte slots on Android, packed by natural alignment on Darwin) and for arguments narrower than 32 bits (`GLboolean`). `glBlitFramebuffer` (10 `int`-sized arguments: `filter` is at `sp+8` for the caller, `sp+4` for the callee), `glTexSubImage3D` and `glCompressedTexSubImage3D` are affected. This follows from the ABI rules and has not been reproduced on a device. ANGLE would not change it, because ANGLE is also built for Darwin.

Target: generate the GL boundary from Khronos's `gl.xml`. Each function is classified as pass-through or thunk (re-pack stack arguments, mask narrow arguments), and the backend behind it (EAGL now, ANGLE later) becomes a build choice.

### JNI

The native runtime's `JavaVM` / `JNIEnv` (`runtime/jni.c`) is a stub: every function returns 0 / `NULL`, `GetVersion` reports 1.6, and `AttachCurrentThread` / `GetEnv` hand out the one environment. Code that only probes JNI keeps running; code that needs a Java object fails.

That is enough for a pure `NativeActivity` app and for nothing else. SDL2, Godot, Unity, Cocos and `GameActivity` all start in a Java `Activity` and call back and forth through JNI, so real JNI is the entry condition for M4, not something that waits for M5. The plan is in §8.

## 7. Code added at runtime

Games that download native code later do not work with AOT conversion alone. However, henge has a signer on the iPhone, so the following may be possible (to be verified):

- When the shim's `dlopen` receives an unconverted ELF, convert, sign, and load it on the spot.
- If a newly signed file can be `dlopen`ed in the same process, no restart is needed.

Self-unpacking packers and DRM that generates code in memory are out of scope.

## 8. Java / Kotlin: henge-vm

The original plan was to port ART and `dex2oat` and AOT-compile dex on the iPhone. What was built instead is `henge-vm` (`vm/`): a Dalvik bytecode interpreter in C with henge's own `java.*` core classes (`vm/core`) and a small `android.*` subset (`vm/android`: `Activity`, `GLSurfaceView`, `GLES20`, `AudioTrack`, `Handler`, ...). It runs Java and Kotlin (kotlinc + R8) tests and a Kotlin sample game (`samples/kcatch`) on macOS. It has no JIT and no AOT, object references are 32-bit handles into a non-moving table, and one global lock serializes all threads.

ART is dropped: its size, its reliance on SIGSEGV for null and stack checks, and the unverified `dex2oat` questions (V7) are avoided, at the price of interpreter speed and of writing the class library.

### 8.1 Role (proposed)

In the games henge targets, Java is glue: the `Activity`, lifecycle, input forwarding, asset and preference access. The frame loop is native. So:

- **Native code runs converted, at full speed. The APK's own dex runs on henge-vm.** The two meet at JNI.
- henge does not reimplement each engine's Java side in C. `SDLActivity.java` or `GodotActivity` from the APK run as they are.
- Scope of `android.*`: what engine glue uses (`Activity`, `SurfaceView` / `GLSurfaceView`, input events, `AssetManager`, `SharedPreferences`, `AudioTrack` / `SoundPool`, `Handler` / `Looper`, `Build`, display metrics). The `View` / layout / resource system for ordinary apps is a non-goal.

### 8.2 The JNI bridge (not implemented)

`vm/` and `runtime/` do not know each other today: the VM has no `System.loadLibrary`, and `runtime/jni.c` is a stub. Connecting them needs:

| Piece | Notes |
| --- | --- |
| `System.loadLibrary` | → `henge_load`, then call `JNI_OnLoad` |
| A real `JNIEnv` over the VM | `FindClass`, `Get*ID`, field access, strings, arrays, `RegisterNatives`, exceptions. `jobject` maps onto the VM's handles (local and global reference tables; the table does not move objects) |
| `Call*Method` (variadic) | Called from Android code with AAPCS64 variadics into Darwin-compiled code: asm entry stubs that spill the registers into an AAPCS64 `va_list`, then share the `...V` implementations (same technique as the `printf` shims) |
| VM → native calls | A `native` method bound by `RegisterNatives` or by `Java_*` name has an arbitrary signature. One static asm bridge loads x0–x7, d0–d7 and the stack from the method's shorty. No code generation is needed |
| Threads | Release the global lock around native calls and blocking operations; `AttachCurrentThread` creates a VM `Thread` for a native thread |
| GC roots | Local references per native frame, global references, pinned array and string contents |

### 8.3 Class library (open decision)

`vm/core` is written from scratch (about 130 classes). R8-processed Kotlin and engine glue reach into `java.util.concurrent`, reflection, `String.format`, `java.io` / `java.nio`, and more. Either keep growing it, driven by what target APKs reference, or adopt AOSP libcore (complete, but tied to ART internals and to native libraries, and GPLv2 with the Classpath exception). Whichever is chosen, a tool that lists the classes and methods an APK's dex references but henge-vm lacks is needed first, the same way `henge-convert` lists missing native imports.

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
