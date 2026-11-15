# Roadmap

## Milestones

| # | Goal | Exit criteria | Rough size | Status |
| --- | --- | --- | --- | --- |
| M0 | Set up the environment | Xcode, Android NDK, and an empty host app running on a device. NDK `.so` files build in `samples/` | 1–2 days | Done: toolchain, `samples/`, host app on the simulator and an iPhone 16 |
| M1 | Call a function in a converted `.so` | [m1-elf-to-macho.md](m1-elf-to-macho.md) §7 | A few days to 1 week | Done: passes on macOS, the iOS Simulator and an iPhone |
| M2 | libc compatibility shim and on-device signing | `.so` files using `malloc`, `printf`, `pthread`, and TLS work. Conversion through execution completes on the iPhone | A few weeks | Done for the samples: patching, TLS, `DT_NEEDED`, pthread / errno / ALooper shims, C++ (libc++, exceptions, RTTI, threads) and on-device signing work; an APK converted on the iPhone runs `android_main`. The shim covers about 200 bionic names, far fewer than a real engine imports ([architecture.md](architecture.md) §5.3) |
| M3 | Run NativeActivity | The NDK `native-activity` sample renders via ANGLE and responds to touch | 1–2 months | Mostly done, without ANGLE: iOS still ships OpenGL ES, so EGL is implemented on EAGL and GLES calls pass through. A sample game (`samples/libgame`) renders at 60 fps on an iPhone with touch, `assets/` and AAudio. Not done: Vulkan, OpenSL ES, GLES extensions, key events, JNI beyond a stub |
| M4 | Run a real game | One SDL2 game is playable, with audio: its native libraries run converted and its own Java glue (`SDLActivity`) runs on henge-vm, joined by JNI | Several months | Not started. Blocked on the prerequisites below |
| M5 | Java / Kotlin on henge-vm | The Activity of a simple Java-only app is displayed | — | Partly done on macOS: `henge-vm` runs Java / Kotlin tests and a Kotlin sample game (`samples/kcatch`). Not connected to the iOS host or to JNI. The ART port originally planned here is dropped ([architecture.md](architecture.md) §8) |

The M4 exit criterion used to read "an SDL2 or Godot game with no Java dependency". There is no such game: on Android both engines start in a Java `Activity` and reach native code through JNI. Only pure `NativeActivity` apps are Java-free, and M3 already covers them.

### Prerequisites for M4

In order. Section numbers refer to [architecture.md](architecture.md).

| # | Item | Why first |
| --- | --- | --- |
| P1 | Verify V3 with a free Apple ID certificate | Decides who can use henge at all |
| P2 | Decide the process model (§5.1) | Fixes repeated runs and decides whether the signing identity can be isolated from guest code |
| P3 | Symbol lookup in bionic order (§5.2) | Engines ship several libraries with overlapping exports |
| P4 | Stubs for undefined symbols, generated shim table, uniform `errno` (§5.3, §6) | Turns "one failed launch per missing function" into a list |
| P5 | Crash reporting (§5.4) | Makes the M4 iteration loop workable |
| P6 | Generated GLES boundary with thunks (§6) | `glBlitFramebuffer` and other 9+ argument calls are wrong today |
| P7 | JNI bridge between `runtime/` and `vm/` (§8.2) | The entry condition for any engine with a Java `Activity` |
| P8 | Conversion cache and file-backed conversion (§4) | Engine libraries are 50–150 MiB |

## Items to verify

Items whose design cannot be finalized until checked on a real device. Verify them with a test app in parallel with M1.

| # | Item | Design affected | When | Result |
| --- | --- | --- | --- | --- |
| V1 | Is `TPIDR_EL0` free, and is it preserved across context switches and signals? | How TLS is implemented ([abi-differences.md](../research/abi-differences.md) §2) | M1 | No, on macOS and on an iPhone 16 (iOS): it holds the CPU number (`0x1004`/`0x1005`); a write is lost after a sleep or signal. henge reads a per-thread pointer from Darwin TSD instead |
| V2 | Does XNU preserve or clobber x18? | Whether to reject `.so` files that use x18 | M1 | Clobbered: on an iPhone 16 x18 reads 0 after `usleep`, a signal, a syscall or a thread switch. The converter now rejects Shadow Call Stack code (`str x30, [x18], #8`); other x18 uses still only warn |
| V3 | Can a dylib signed on the iPhone be `dlopen`ed from the container (with a free Apple ID certificate)? | The premise of the whole approach | Start of M2 | Yes with a developer identity of the app's team (paid program, Debug build, no debugger): a dylib signed **on the device** with the imported `.p12` loads from the container. An **ad hoc** signature is rejected (`code signature invalid`). A free Apple ID certificate is not tried |
| V4 | Can newly signed dylibs be `dlopen`ed one after another in the same process? | Handling of code added at runtime ([architecture.md](architecture.md) §7) | M2 | Not yet: a dylib signed while the process already runs guest code has not been tried |
| V5 | Does dyld accept a dylib with no libSystem dependency and empty chained fixups? | How the Mach-O is assembled | M1 | macOS, iOS Simulator and iPhone: accepted |
| V6 | Is the `increased-memory-limit` entitlement available to free accounts? | Whether large games can be handled | M3 | Not tried. Without it an iPhone 16 (8 GB) reports about 3.3 GB available to the app (`os_proc_available_memory`) |
| V7 | Can dex2oat disable implicit null checks and stack checks? | Whether ART can avoid relying on SIGSEGV | M5 | Dropped with the ART port |
| V8 | How much memory does an app extension get, and can it `dlopen` a dylib signed on the device? | Process model ([architecture.md](architecture.md) §5.1) | Before M4 | — |
| V9 | Do the GLES calls with 9+ arguments misbehave through the pass-through as the ABI rules predict? | GLES boundary ([architecture.md](architecture.md) §6) | Before M4 | — |

## Non-goals

- APKs with only 32-bit ARM (`armeabi-v7a`)
- x86 / x86_64 `.so` files
- Real Google Play Services features (login, billing, cloud saves)
- Games with anti-cheat or DRM
- Ordinary apps built on the Android `View` / layout / resource system
- Porting ART or `dex2oat`
- Distribution via the App Store
