import SwiftUI

@main
struct HengeApp: App {
    @StateObject private var runner = M1Runner()
    @StateObject private var library = APKLibrary()
    @StateObject private var converter = ConvertRunner()

    init() {
        // Platform shims for Android's EGL and AAudio, before any library is loaded.
        henge_register_egl_shims()
        henge_register_audio_shims()
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(runner)
                .environmentObject(library)
                .environmentObject(converter)
                // Run the M1 self-test at every launch (scripts grep stdout for its summary line);
                // it runs off the main thread so the first frame isn't blocked.
                .task { if runner.state == .idle { runner.run() } }
                // `-probe` prints the V1 / V2 / V6 platform probes (roadmap) to stdout.
                .task { if UserDefaults.standard.bool(forKey: "probe") { _ = henge_run_probes() } }
                // `-convertRun sample.apk` runs Convert & Run on that library APK at launch, so it can be
                // driven from `devicectl device process launch --console` without tapping.
                .task {
                    guard let name = UserDefaults.standard.string(forKey: "convertRun"),
                          converter.state == .idle else { return }
                    let seconds = UserDefaults.standard.double(forKey: "convertRunSeconds")
                    let display = UserDefaults.standard.bool(forKey: "display")
                    converter.run(apk: library.directory.appendingPathComponent(name),
                                  stopAfter: seconds > 0 ? seconds : (display ? nil : 1), display: display)
                }
        }
    }
}
