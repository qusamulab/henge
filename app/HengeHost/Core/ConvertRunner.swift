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
    @Published private(set) var apkURL: URL?
    @Published private(set) var requiresRestart = false
    @Published private(set) var previousCrash: String?

    private var activity: OpaquePointer?
    private var vmHost: OpaquePointer?
    private var loaded: LoadedLibrary?
    /// True while a game screen is up (the run was started with `display`).
    @Published private(set) var presenting = false
    private var displaying = false
    private var autoStop: Task<Void, Never>?

    func refreshProcessState() { requiresRestart = henge_guest_started() != 0 }

    init() {
        let fm = FileManager.default
        let directory = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("henge", isDirectory: true)
        do {
            try fm.createDirectory(at: directory, withIntermediateDirectories: true)
            let path = directory.appendingPathComponent("last-crash.bin").path
            var report = [CChar](repeating: 0, count: 16384)
            if henge_crash_read(path, &report, report.count) == 1 { previousCrash = String(cString: report) }
            var err = [CChar](repeating: 0, count: 512)
            if henge_crash_install(path, &err, err.count) != 0 { print("crash recorder: \(String(cString: err))") }
        } catch { print("crash recorder: \(error.localizedDescription)") }
    }

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
        guard henge_guest_started() == 0 else { requiresRestart = true; return }
        state = .running
        apkURL = apk.resolvingSymlinksInPath().standardizedFileURL
        displaying = display
        lines = []
        failures = 0
        let session = ConvertSession(apk: apk, display: display) { [weak self] line, failed in
            DispatchQueue.main.async { self?.append(line, failed: failed) }
        }
        Task.detached(priority: .userInitiated) {
            let outcome = session.run()
            await MainActor.run {
                self.requiresRestart = henge_guest_started() != 0
                if case .needsSurface(let lib) = outcome {
                    self.loaded = lib
                    self.presenting = true
                    self.state = .awaitingSurface
                    return
                }
                var activity: OpaquePointer?
                if case .running(let a) = outcome { activity = a }
                if case .runningVM(let vm) = outcome { self.vmHost = vm }
                self.activity = activity
                if activity != nil || self.vmHost != nil {
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
            let vm = lib.java ? startVM(apk: lib.apkPath, layer: layer, width: width, height: height, err: &err) : nil
            let activity: OpaquePointer? = lib.java ? nil : lib.dataPath.withCString { data in
                lib.apkPath.withCString { apk in
                    var opts = henge_activity_opts(data_path: data, apk_path: apk, layer: layer, width: Int32(width), height: Int32(height))
                    return henge_activity_start(lib.image, &opts, &err, err.count)
                }
            }
            await MainActor.run {
                if activity != nil || vm != nil {
                    self.activity = activity
                    self.vmHost = vm
                    self.append("PASS  \(lib.java ? "vm_host_start" : "henge_activity_start") (\(width)x\(height) surface)", failed: false)
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
        if let vmHost { vm_host_resize(vmHost, Int32(width), Int32(height)) }
    }

    func touch(_ phase: henge_touch_phase, id: Int32, x: Float, y: Float) {
        if let activity { henge_activity_touch(activity, phase, id, x, y) }
        if let vmHost { vm_host_touch(vmHost, Int32(phase.rawValue), id, x, y) }
    }

    /// Drives the running NativeActivity down (onPause ... onDestroy).
    func stop() {
        if state == .awaitingSurface { loaded = nil; presenting = false; append("note  closed before the surface was ready", failed: false); finish(); return }
        guard state == .active else { return }
        let activity = self.activity, vm = self.vmHost
        presenting = false
        autoStop?.cancel()
        self.activity = nil
        self.vmHost = nil
        state = .running
        Task.detached(priority: .userInitiated) {
            if let activity { henge_activity_stop(activity) }
            if let vm { vm_host_stop(vm) }
            await MainActor.run {
                self.append("PASS  guest stopped (onDestroy returned)", failed: false)
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
    let image: OpaquePointer?
    let dataPath: String
    let apkPath: String
    var java = false
}

private func startVM(apk: String, layer: UnsafeMutableRawPointer? = nil,
                     width: Int = 640, height: Int = 480, err: inout [CChar]) -> OpaquePointer? {
    apk.withCString { path in
        var opts = vm_host_opts()
        opts.apk_path = path
        opts.layer = layer
        opts.width = Int32(width)
        opts.height = Int32(height)
        opts.log = { priority, tag, message, _ in
            // VM log output shares the native runtime's persistent process log sink.
            henge_log_message(priority, tag, message)
        }
        return vm_host_start(&opts, &err, err.count)
    }
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
        case runningVM(OpaquePointer)
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

        let metadata: APKMetadata
        do {
            metadata = try APKMetadata(reading: handle)
        } catch {
            fail("read APK information: \(error.localizedDescription)")
            return .finished
        }
        let package = metadata.package
        pass("manifest: \(package)")
        if let issue = metadata.executionIssue {
            fail(issue)
            return .finished
        }

        let fm = FileManager.default
        let base = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dataDirectory = base.appendingPathComponent("henge/data/\(package)", isDirectory: true)
        let prepared: APKPreparation.Prepared
        let dir: URL
        do {
            prepared = try APKPreparation.prepare(apk)
            for library in prepared.libraries {
                for symbol in prepared.unresolved(in: library) {
                    emit("note  \(library.name): unresolved \(symbol.name)\(symbol.weak ? " (weak)" : symbol.type == 2 ? " (aborts if called)" : " (blocks loading)")")
                }
            }
            guard prepared.issues.isEmpty else {
                for issue in prepared.issues { fail(issue) }
                return .finished
            }
            if UserDefaults.standard.bool(forKey: "usePrebuilt") {
                dir = base.appendingPathComponent("henge/run/\(package)", isDirectory: true)
            } else {
                dir = try APKPreparation.sign(prepared, signer: signer.map { UnsafePointer($0.pointer) })
            }
            try fm.createDirectory(at: dataDirectory, withIntermediateDirectories: true)
            pass("prepared \(prepared.libraries.count) libraries (conversion/signing cache)")
        } catch {
            fail(error.localizedDescription)
            return .finished
        }
        guard henge_guest_begin(&err, err.count) == 0 else {
            fail(cString(err))
            return .finished
        }

        // The guest can request more ELF files after startup. Retain the signing context for
        // the process lifetime because guest workers may still call dlopen after Stop.
        let loader = Unmanaged.passRetained(RuntimeELFLoader(signer: signer))
        signer = nil // ownership transferred to the loader
        henge_set_elf_prepare({ input, output, capacity, user, errorPointer, errorLength in
            guard let input, let output, let user else { return -1 }
            let loader = Unmanaged<RuntimeELFLoader>.fromOpaque(user).takeUnretainedValue()
            do {
                let url = try APKPreparation.prepareLibrary(URL(fileURLWithPath: String(cString: input)),
                    signer: loader.signer.map { UnsafePointer($0.pointer) })
                let bytes = url.path.utf8CString
                guard bytes.count <= capacity else { throw APKPreparation.PreparationError(message: "Converted path is too long.") }
                bytes.withUnsafeBufferPointer { output.update(from: $0.baseAddress!, count: $0.count) }
                return 0
            } catch {
                if let errorBuffer = errorPointer, errorLength > 0 {
                    let bytes = error.localizedDescription.utf8CString
                    bytes.withUnsafeBufferPointer { errorBuffer.update(from: $0.baseAddress!, count: min($0.count - 1, errorLength - 1)) }
                    errorBuffer[min(bytes.count - 1, errorLength - 1)] = 0
                }
                return -1
            }
        }, loader.toOpaque())

        // Android code's log lines show up in the result.
        let box = Unmanaged.passRetained(LogBox(emit: { self.emit($0) }))
        henge_set_log_sink({ _, tag, msg, user in
            let box = Unmanaged<LogBox>.fromOpaque(user!).takeUnretainedValue()
            box.emit("[\(String(cString: tag!))] \(String(cString: msg!))")
        }, box.toOpaque())

        henge_set_library_path(dir.path)
        if !metadata.nativeActivity && metadata.hasDex {
            if display {
                return .needsSurface(LoadedLibrary(image: nil, dataPath: dataDirectory.path, apkPath: apk.path, java: true))
            }
            guard let vm = startVM(apk: apk.path, err: &err) else {
                fail("vm_host_start: \(cString(err))")
                return .finished
            }
            pass("vm_host_start (launcher Activity is running)")
            return .runningVM(vm)
        }
        guard let target = metadata.nativeActivity ? metadata.entryLibrary : prepared.libraries.first?.name else {
            fail("No entry point was found.")
            return .finished
        }
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
                return .needsSurface(LoadedLibrary(image: img, dataPath: dataDirectory.path, apkPath: apk.path))
            }
            var opts = henge_activity_opts()
            guard let activity = dataDirectory.path.withCString({ data in
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

    private final class RuntimeELFLoader {
        let signer: SigningStore.Signer?
        init(signer: SigningStore.Signer?) { self.signer = signer }
        deinit { signer?.free() }
    }

    private final class LogBox {
        let emit: (String) -> Void
        init(emit: @escaping (String) -> Void) { self.emit = emit }
    }

}
