import Foundation
import os

/// APK -> lib/arm64-v8a/*.so -> Mach-O (henge_convert) -> signature with the imported developer identity
/// -> dlopen + henge_link -> (NativeActivity: android_main keeps running until stopped), all inside the
/// app. Mirrors what `henge-apk extract` + `henge-convert --sign-identity` + test_m1 do on the Mac.
@MainActor
final class ConvertRunner: ObservableObject {
    enum State { case idle, running, awaitingSurface, active, finished }

    @Published private(set) var state: State = .idle
    @Published private(set) var lines: [String] = []
    @Published private(set) var failures = 0

    private var activity: OpaquePointer?
    private var loaded: LoadedLibrary?
    /// True while a game screen is up (the run was started with `display`).
    @Published private(set) var presenting = false
    private var displaying = false
    private var autoStop: Task<Void, Never>?

    /// Exactly `PASS: convert-run (0 failures)` / `FAIL: convert-run (N failures)`; scripts grep for it.
    var summaryLine: String {
        "\(failures == 0 ? "PASS" : "FAIL"): convert-run (\(failures) failure\(failures == 1 ? "" : "s"))"
    }

    var logText: String { (lines + (state == .finished ? [summaryLine] : [])).joined(separator: "\n") }

    /// Converts and loads the APK. A NativeActivity then keeps running (state .active) until `stop()`;
    /// with `stopAfter` it is stopped automatically (for runs driven from a script).
    ///
    /// With `display`, a NativeActivity gets a full-screen OpenGL ES surface (`GameScreen`) and touches;
    /// without it, a placeholder window.
    func run(apk: URL, stopAfter: TimeInterval? = nil, display: Bool = false) {
        guard state != .running, state != .active, state != .awaitingSurface else { return }
        state = .running
        displaying = display
        lines = []
        failures = 0
        let session = ConvertSession(apk: apk, display: display) { [weak self] line, failed in
            DispatchQueue.main.async { self?.append(line, failed: failed) }
        }
        Task.detached(priority: .userInitiated) {
            let outcome = session.run()
            await MainActor.run {
                if case .needsSurface(let lib) = outcome {
                    self.loaded = lib
                    self.presenting = true
                    self.state = .awaitingSurface
                    return
                }
                var activity: OpaquePointer?
                if case .running(let a) = outcome { activity = a }
                self.activity = activity
                if activity != nil {
                    self.state = .active
                    if let stopAfter {
                        self.autoStop = Task { [weak self] in
                            try? await Task.sleep(nanoseconds: UInt64(stopAfter * 1e9))
                            self?.stop()
                        }
                    }
                } else {
                    self.finish()
                }
            }
        }
    }

    /// The game surface is on screen: start the activity on it.
    func startActivity(layer: UnsafeMutableRawPointer, width: Int, height: Int) {
        guard state == .awaitingSurface, let lib = loaded else { return }
        loaded = nil
        state = .running
        Task.detached(priority: .userInitiated) {
            var err = [CChar](repeating: 0, count: 512)
            let activity: OpaquePointer? = lib.dataPath.withCString { data in
                lib.apkPath.withCString { apk in
                    var opts = henge_activity_opts(data_path: data, apk_path: apk, layer: layer, width: Int32(width), height: Int32(height))
                    return henge_activity_start(lib.image, &opts, &err, err.count)
                }
            }
            await MainActor.run {
                if let activity {
                    self.activity = activity
                    self.append("PASS  henge_activity_start (\(width)x\(height) surface, android_main is running)", failed: false)
                    self.state = .active
                } else {
                    self.append("FAIL  henge_activity_start: \(String(cString: err))", failed: true)
                    self.presenting = false
                    self.finish()
                }
            }
        }
    }

    func resize(width: Int, height: Int) {
        if let activity { henge_activity_resize(activity, Int32(width), Int32(height)) }
    }

    func touch(_ phase: henge_touch_phase, id: Int32, x: Float, y: Float) {
        if let activity { henge_activity_touch(activity, phase, id, x, y) }
    }

    /// Drives the running NativeActivity down (onPause ... onDestroy).
    func stop() {
        if state == .awaitingSurface { loaded = nil; presenting = false; append("note  closed before the surface was ready", failed: false); finish(); return }
        guard state == .active, let activity else { return }
        presenting = false
        autoStop?.cancel()
        self.activity = nil
        state = .running
        Task.detached(priority: .userInitiated) {
            henge_activity_stop(activity)
            await MainActor.run {
                self.append("PASS  henge_activity_stop (onDestroy returned, android_main exited)", failed: false)
                self.finish()
            }
        }
    }

    private func finish() {
        state = .finished
        print(summaryLine)
        fflush(stdout)
    }

    private func append(_ line: String, failed: Bool) {
        if state == .running || state == .active { print(line) }
        lines.append(line)
        if failed { failures += 1 }
    }
}

/// A library that is loaded and waiting for a window.
struct LoadedLibrary: @unchecked Sendable {
    let image: OpaquePointer
    let dataPath: String
    let apkPath: String
}

/// One conversion + load, on a background thread. Reports each line through `emit`.
private final class ConvertSession {
    private static let log = Logger(subsystem: Bundle.main.bundleIdentifier ?? "henge", category: "convert-run")

    private let apk: URL
    private let display: Bool
    private let sink: (String, Bool) -> Void
    private var signer: SigningStore.Signer?

    init(apk: URL, display: Bool, sink: @escaping (String, Bool) -> Void) {
        self.apk = apk
        self.display = display
        self.sink = sink
    }

    private func emit(_ line: String, failed: Bool = false) {
        print(line)
        Self.log.log("\(line, privacy: .public)")
        sink(line, failed)
    }

    private func pass(_ line: String) { emit("PASS  \(line)") }
    private func fail(_ line: String) { emit("FAIL  \(line)", failed: true) }
    private func cString(_ buf: [CChar]) -> String { String(cString: buf) }

    enum Outcome {
        case finished
        case running(OpaquePointer)
        case needsSurface(LoadedLibrary)
    }

    func run() -> Outcome {
        signer = SigningStore.loadSigner()
        defer {
            signer?.free()
            fflush(stdout)
        }
        if let signer {
            pass("signing identity: \(signer.summary)")
        } else {
            emit("note  no signing identity imported (Signing tab): libraries get an ad hoc signature, which iOS rejects")
        }
        return body()
    }

    private func body() -> Outcome {
        var err = [CChar](repeating: 0, count: 512)
        guard let handle = henge_apk_open(apk.path, &err, err.count) else {
            fail("henge_apk_open(\(apk.lastPathComponent)): \(cString(err))")
            return .finished
        }
        defer { henge_apk_close(handle) }
        pass("henge_apk_open(\(apk.lastPathComponent))")

        var manifest = henge_apk_manifest()
        guard henge_apk_read_manifest(handle, &manifest, &err, err.count) == 0 else {
            fail("henge_apk_read_manifest: \(cString(err))")
            return .finished
        }
        let package = withUnsafePointer(to: &manifest.package) {
            $0.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
        }
        pass("manifest: \(package)")

        var idx: UnsafeMutablePointer<Int>?
        var n = 0
        guard henge_apk_native_libs(handle, "arm64-v8a", &idx, &n) == 0, n > 0, let idx else {
            fail("no lib/arm64-v8a/*.so in the APK")
            return .finished
        }
        defer { free(idx) }

        let fm = FileManager.default
        let base = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("henge/run/\(package)", isDirectory: true)
        // `-usePrebuilt 1`: keep dylibs already in the run directory (signed on the Mac) instead of converting.
        let usePrebuilt = UserDefaults.standard.bool(forKey: "usePrebuilt")
        if !usePrebuilt { try? fm.removeItem(at: dir) }
        do { try fm.createDirectory(at: dir, withIntermediateDirectories: true) } catch {
            fail("create \(dir.path): \(error.localizedDescription)")
            return .finished
        }

        let mainLib = "lib\(libName(&manifest)).so"
        var firstSo: String?
        for k in 0..<n {
            let i = idx[k]
            guard let cname = henge_apk_name(handle, i) else { continue }
            let soName = (String(cString: cname) as NSString).lastPathComponent
            let stem = soName.hasSuffix(".so") ? String(soName.dropLast(3)) : soName
            let out = dir.appendingPathComponent("\(stem).dylib")
            if firstSo == nil || (manifest.native_activity != 0 && soName == mainLib) { firstSo = soName }
            if usePrebuilt && fm.fileExists(atPath: out.path) {
                pass("prebuilt \(stem).dylib")
                continue
            }
            var elf: UnsafeMutablePointer<UInt8>?
            var elfSize = 0
            guard henge_apk_extract(handle, i, &elf, &elfSize, &err, err.count) == 0, let elf else {
                fail("extract \(soName): \(cString(err))")
                return .finished
            }
            defer { free(elf) }
            guard let macho = convert(elf: elf, size: elfSize, soName: soName, stem: stem) else { return .finished }
            do { try macho.write(to: out) } catch {
                fail("write \(out.lastPathComponent): \(error.localizedDescription)")
                return .finished
            }
        }
        guard let target = firstSo else { return .finished }

        // Android code's log lines show up in the result.
        let box = Unmanaged.passRetained(LogBox(emit: { [weak self] in self?.emit($0) }))
        henge_set_log_sink({ _, tag, msg, user in
            let box = Unmanaged<LogBox>.fromOpaque(user!).takeUnretainedValue()
            box.emit("[\(String(cString: tag!))] \(String(cString: msg!))")
        }, box.toOpaque())

        henge_set_library_path(dir.path)
        guard let img = henge_load(target, &err, err.count) else {
            let msg = cString(err)
            fail("henge_load(\(target)): \(msg)")
            if msg.contains("code signature invalid") {
                emit("      iOS rejected the signature. Libraries in the app container must be signed by your developer")
                emit("      team: import your .p12 in the Signing tab, then run again.")
            }
            return .finished
        }
        pass("henge_load(\(target))")

        // A NativeActivity library: play the framework and run android_main.
        if henge_dlsym(img, "ANativeActivity_onCreate") != nil {
            if display {
                return .needsSurface(LoadedLibrary(image: img, dataPath: dir.path, apkPath: apk.path))
            }
            var opts = henge_activity_opts()
            guard let activity = dir.path.withCString({ data in
                apk.path.withCString { apkPath in
                    opts.data_path = data
                    opts.apk_path = apkPath
                    return henge_activity_start(img, &opts, &err, err.count)
                }
            }) else {
                fail("henge_activity_start: \(cString(err))")
                return .finished
            }
            pass("henge_activity_start (ANativeActivity_onCreate returned, android_main is running)")
            return .running(activity)
        }

        // The sample APK's libadd: call into the original Android code.
        if let p = henge_dlsym(img, "add") {
            typealias II_I = @convention(c) (Int32, Int32) -> Int32
            let r = unsafeBitCast(p, to: II_I.self)(2, 3)
            if r == 5 { pass("add(2, 3)                    = \(r)") } else { fail("add(2, 3) = \(r) (expected 5)") }
        } else {
            pass("no `add` export; skipped the sample call")
        }
        return .finished
    }

    private final class LogBox {
        let emit: (String) -> Void
        init(emit: @escaping (String) -> Void) { self.emit = emit }
    }

    private func libName(_ m: inout henge_apk_manifest) -> String {
        withUnsafePointer(to: &m.lib_name) {
            $0.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
        }
    }

    /// henge_convert + signature. Returns the signed Mach-O.
    private func convert(elf: UnsafeMutablePointer<UInt8>, size: Int, soName: String, stem: String) -> Data? {
        var err = [CChar](repeating: 0, count: 512)
        var result = henge_convert_result()
        let rc: Int32 = "@rpath/\(stem).dylib".withCString { installName in
            var opts = henge_convert_opts(platform: HENGE_PLATFORM_IOS, install_name: installName, link_libsystem: 0)
            return henge_convert(elf, size, &opts, &result, &err, err.count)
        }
        guard rc == 0 else {
            fail("henge_convert(\(soName)): \(cString(err))")
            return nil
        }
        defer { henge_convert_result_free(&result) }

        var signed: UnsafeMutablePointer<UInt8>?
        var signedSize = 0
        let rc2: Int32 = signer.map {
            henge_sign_identity(result.macho, result.macho_size, stem, $0.pointer, &signed, &signedSize, &err, err.count)
        } ?? henge_sign_adhoc(result.macho, result.macho_size, stem, &signed, &signedSize, &err, err.count)
        guard rc2 == 0, let signed else {
            fail("sign \(soName): \(cString(err))")
            return nil
        }
        pass("convert+sign \(soName) -> \(stem).dylib (\(signedSize) bytes, svc \(result.svc_count), tpidr_el0 \(result.tpidr_count) patched)")
        return Data(bytesNoCopy: signed, count: signedSize, deallocator: .free)
    }
}
