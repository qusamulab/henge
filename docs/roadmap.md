# Roadmap

## Milestones

| # | Goal | Exit criteria | Rough size | Status |
| --- | --- | --- | --- | --- |
| M0 | Set up the environment | Xcode, Android NDK, and an empty host app running on a device. NDK `.so` files build in `samples/` | 1–2 days | Done: toolchain, `samples/`, host app on the simulator and an iPhone 16 |
| M1 | Call a function in a converted `.so` | [m1-elf-to-macho.md](m1-elf-to-macho.md) §7 | A few days to 1 week | Done: passes on macOS, the iOS Simulator and an iPhone |
| M2 | libc compatibility shim and on-device signing | `.so` files using `malloc`, `printf`, `pthread`, and TLS work. Conversion through execution completes on the iPhone | A few weeks | In progress: patching, TLS, `DT_NEEDED`, pthread / errno / ALooper shims, a NativeActivity host and on-device signing work; an APK converted on the iPhone runs `android_main`. Missing: display, input, JNI |
| M3 | Run NativeActivity | The NDK `native-activity` sample renders via ANGLE and responds to touch | 1–2 months | Mostly done, without ANGLE: iOS still ships OpenGL ES, so EGL is implemented on EAGL and GLES calls pass through. A sample game (`samples/libgame`) renders at 60 fps on an iPhone with touch, `assets/` and AAudio. Not done: Vulkan, OpenSL ES, GLES extensions, key events, JNI beyond a stub |
| M4 | Run a real game | One SDL2 or Godot game with no Java dependency is playable, with audio | Several months | — |
| M5 | Support ART | The Activity of a simple Java-only app is displayed | Years | — |

## Items to verify

Items whose design cannot be finalized until checked on a real device. Verify them with a test app in parallel with M1.

| # | Item | Design affected | When | Result |
| --- | --- | --- | --- | --- |
| V1 | Is `TPIDR_EL0` free, and is it preserved across context switches and signals? | How TLS is implemented ([abi-differences.md](../research/abi-differences.md) §2) | M1 | No, on macOS and on an iPhone 16 (iOS): it holds the CPU number (`0x1004`/`0x1005`); a write is lost after a sleep or signal. henge reads a per-thread pointer from Darwin TSD instead |
| V2 | Does XNU preserve or clobber x18? | Whether to reject `.so` files that use x18 | M1 | Clobbered: on an iPhone 16 x18 reads 0 after `usleep`, a signal, a syscall or a thread switch. The converter now rejects Shadow Call Stack code (`str x30, [x18], #8`); other x18 uses still only warn |
| V3 | Can a dylib signed on the iPhone be `dlopen`ed from the container (with a free Apple ID certificate)? | The premise of the whole approach | Start of M2 | Yes with a developer identity of the app's team (paid program, Debug build, no debugger): a dylib signed **on the device** with the imported `.p12` loads from the container. An **ad hoc** signature is rejected (`code signature invalid`). A free Apple ID certificate is not tried |
| V4 | Can newly signed dylibs be `dlopen`ed one after another in the same process? | Handling of code added at runtime ([architecture.md](architecture.md) §7) | M2 | Not yet: only one dylib at a time was loaded |
| V5 | Does dyld accept a dylib with no libSystem dependency and empty chained fixups? | How the Mach-O is assembled | M1 | macOS, iOS Simulator and iPhone: accepted |
| V6 | Is the `increased-memory-limit` entitlement available to free accounts? | Whether large games can be handled | M3 | Not tried. Without it an iPhone 16 (8 GB) reports about 3.3 GB available to the app (`os_proc_available_memory`) |
| V7 | Can dex2oat disable implicit null checks and stack checks? | Whether ART can avoid relying on SIGSEGV | M5 | — |

## Non-goals

- APKs with only 32-bit ARM (`armeabi-v7a`)
- x86 / x86_64 `.so` files
- Real Google Play Services features (login, billing, cloud saves)
- Games with anti-cheat or DRM
- Distribution via the App Store
