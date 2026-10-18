import Foundation
import os

/// Result of one Convert & Run.
struct ConvertReport: Sendable {
    var lines: [String] = []
    var failures = 0
    /// Exactly `PASS: convert-run (0 failures)` / `FAIL: convert-run (N failures)`; scripts grep for it.
    var summaryLine: String {
        "\(failures == 0 ? "PASS" : "FAIL"): convert-run (\(failures) failure\(failures == 1 ? "" : "s"))"
    }
}

/// APK -> lib/arm64-v8a/*.so -> Mach-O (henge_convert) -> ad hoc signature -> dlopen + henge_link,
/// all inside the app. Mirrors what `henge-apk extract` + `henge-convert --sign` + test_m1 do on the Mac.
@MainActor
final class ConvertRunner: ObservableObject {
    enum State { case idle, running, finished }

    @Published private(set) var state: State = .idle
    @Published private(set) var report = ConvertReport()

    var logText: String {
        (report.lines + (state == .finished ? [report.summaryLine] : [])).joined(separator: "\n")
    }

    func run(apk: URL) {
        guard state != .running else { return }
        state = .running
        report = ConvertReport()
        Task.detached(priority: .userInitiated) {
            let result = ConvertSession(apk: apk).run()
            await MainActor.run {
                self.report = result
                self.state = .finished
            }
        }
    }
}

private final class ConvertSession {
    private static let log = Logger(subsystem: Bundle.main.bundleIdentifier ?? "henge", category: "convert-run")

    private let apk: URL
    private var report = ConvertReport()

    init(apk: URL) { self.apk = apk }

    private func emit(_ line: String) {
        print(line)
        Self.log.log("\(line, privacy: .public)")
        report.lines.append(line)
    }

    private func pass(_ line: String) { emit("PASS  \(line)") }

    private func fail(_ line: String) {
        report.failures += 1
        emit("FAIL  \(line)")
    }

    private func cString(_ buf: [CChar]) -> String { String(cString: buf) }

    func run() -> ConvertReport {
        body()
        emit(report.summaryLine)
        fflush(stdout)
        return report
    }

    private func body() {
        var err = [CChar](repeating: 0, count: 512)
        guard let handle = henge_apk_open(apk.path, &err, err.count) else {
            fail("henge_apk_open(\(apk.lastPathComponent)): \(cString(err))")
            return
        }
        defer { henge_apk_close(handle) }
        pass("henge_apk_open(\(apk.lastPathComponent))")

        var manifest = henge_apk_manifest()
        guard henge_apk_read_manifest(handle, &manifest, &err, err.count) == 0 else {
            fail("henge_apk_read_manifest: \(cString(err))")
            return
        }
        let package = withUnsafePointer(to: &manifest.package) {
            $0.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
        }
        pass("manifest: \(package)")

        var idx: UnsafeMutablePointer<Int>?
        var n = 0
        guard henge_apk_native_libs(handle, "arm64-v8a", &idx, &n) == 0, n > 0, let idx else {
            fail("no lib/arm64-v8a/*.so in the APK")
            return
        }
        defer { free(idx) }

        let fm = FileManager.default
        let base = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("henge/run/\(package)", isDirectory: true)
        // `-usePrebuilt 1`: keep dylibs already in the run directory (signed on the Mac with a
        // developer identity) instead of converting; on-device identity signing isn't implemented, and
        // iOS rejects the ad hoc signature henge_sign_adhoc produces for libraries loaded from the container.
        let usePrebuilt = UserDefaults.standard.bool(forKey: "usePrebuilt")
        if !usePrebuilt { try? fm.removeItem(at: dir) }
        do { try fm.createDirectory(at: dir, withIntermediateDirectories: true) } catch {
            fail("create \(dir.path): \(error.localizedDescription)")
            return
        }

        var firstSo: String?
        for k in 0..<n {
            let i = idx[k]
            guard let cname = henge_apk_name(handle, i) else { continue }
            let soName = (String(cString: cname) as NSString).lastPathComponent
            var elf: UnsafeMutablePointer<UInt8>?
            var elfSize = 0
            guard henge_apk_extract(handle, i, &elf, &elfSize, &err, err.count) == 0, let elf else {
                fail("extract \(soName): \(cString(err))")
                return
            }
            defer { free(elf) }

            let stem = soName.hasSuffix(".so") ? String(soName.dropLast(3)) : soName
            let out = dir.appendingPathComponent("\(stem).dylib")
            if usePrebuilt && fm.fileExists(atPath: out.path) {
                pass("prebuilt \(stem).dylib")
                if firstSo == nil || (manifest.native_activity != 0 && soName == "lib\(libName(&manifest)).so") {
                    firstSo = soName
                }
                continue
            }
            guard let macho = convert(elf: elf, size: elfSize, soName: soName, stem: stem) else { return }
            do { try macho.write(to: out) } catch {
                fail("write \(out.lastPathComponent): \(error.localizedDescription)")
                return
            }
            if firstSo == nil || (manifest.native_activity != 0 && soName == "lib\(libName(&manifest)).so") {
                firstSo = soName
            }
        }
        guard let target = firstSo else { return }

        henge_set_library_path(dir.path)
        guard let img = henge_load(target, &err, err.count) else {
            let msg = cString(err)
            fail("henge_load(\(target)): \(msg)")
            if msg.contains("code signature invalid") {
                emit("      iOS rejects the ad hoc signature henge puts on a converted library: loading from the app")
                emit("      container needs your developer identity, and signing with it on the device is not")
                emit("      implemented yet. Sign the dylib on the Mac and launch with -usePrebuilt 1 (see README).")
            }
            return
        }
        pass("henge_load(\(target))")

        // A NativeActivity library: play the framework and run android_main.
        if henge_dlsym(img, "ANativeActivity_onCreate") != nil {
            runActivity(img, dataDir: dir)
            return
        }

        // The sample APK's libadd: call into the original Android code.
        if let p = henge_dlsym(img, "add") {
            typealias II_I = @convention(c) (Int32, Int32) -> Int32
            let r = unsafeBitCast(p, to: II_I.self)(2, 3)
            if r == 5 { pass("add(2, 3)                    = \(r)") } else { fail("add(2, 3) = \(r) (expected 5)") }
        } else {
            pass("no `add` export; skipped the sample call")
        }
    }

    private func runActivity(_ img: OpaquePointer, dataDir: URL) {
        var err = [CChar](repeating: 0, count: 512)
        guard let activity = henge_activity_start(img, dataDir.path, &err, err.count) else {
            fail("henge_activity_start: \(cString(err))")
            return
        }
        pass("henge_activity_start (ANativeActivity_onCreate returned, android_main is running)")
        // Let the app thread process the lifecycle commands before tearing it down.
        Thread.sleep(forTimeInterval: 0.5)
        henge_activity_stop(activity)
        pass("henge_activity_stop (onDestroy returned, android_main exited)")
    }

    private func libName(_ m: inout henge_apk_manifest) -> String {
        withUnsafePointer(to: &m.lib_name) {
            $0.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
        }
    }

    /// henge_convert + ad hoc signature. Returns the signed Mach-O.
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
        guard henge_sign_adhoc(result.macho, result.macho_size, stem, &signed, &signedSize, &err, err.count) == 0,
              let signed else {
            fail("henge_sign_adhoc(\(soName)): \(cString(err))")
            return nil
        }
        pass("convert+sign \(soName) -> \(stem).dylib (\(signedSize) bytes, svc \(result.svc_count), tpidr_el0 \(result.tpidr_count) patched)")
        return Data(bytesNoCopy: signed, count: signedSize, deallocator: .free)
    }
}
