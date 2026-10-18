import SwiftUI

@main
struct HengeApp: App {
    @StateObject private var runner = M1Runner()
    @StateObject private var library = APKLibrary()
    @StateObject private var converter = ConvertRunner()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(runner)
                .environmentObject(library)
                .environmentObject(converter)
                // Run the M1 self-test at every launch (scripts grep stdout for its summary line);
                // it runs off the main thread so the first frame isn't blocked.
                .task { if runner.state == .idle { runner.run() } }
                // `-convertRun sample.apk` runs Convert & Run on that library APK at launch, so it can be
                // driven from `devicectl device process launch --console` without tapping.
                .task {
                    guard let name = UserDefaults.standard.string(forKey: "convertRun"),
                          converter.state == .idle else { return }
                    let seconds = UserDefaults.standard.double(forKey: "convertRunSeconds")
                    converter.run(apk: library.directory.appendingPathComponent(name), stopAfter: seconds > 0 ? seconds : 1)
                }
        }
    }
}
