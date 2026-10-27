import Foundation

@main
@MainActor
struct LibraryTests {
    private static var failures = 0

    private static func check(_ condition: Bool, _ message: String) {
        print("\(condition ? "PASS" : "FAIL")  \(message)")
        if !condition { failures += 1 }
    }

    /// Build small real ZIP fixtures using the sample's compiled binary manifest.
    private static func archive(at url: URL, entries: [String: Data], in directory: URL) throws {
        let fm = FileManager.default
        let staging = directory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try fm.createDirectory(at: staging, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: staging) }
        for (name, data) in entries {
            let file = staging.appendingPathComponent(name)
            try fm.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try data.write(to: file)
        }
        let zip = Process()
        zip.executableURL = URL(fileURLWithPath: "/usr/bin/zip")
        zip.currentDirectoryURL = staging
        zip.arguments = ["-q", "-r", url.path, "."]
        try zip.run()
        zip.waitUntilExit()
        guard zip.terminationStatus == 0 else { throw CocoaError(.fileWriteUnknown) }
    }

    private static func manifest(in sample: URL) throws -> Data {
        var error = [CChar](repeating: 0, count: 512)
        guard let apk = henge_apk_open(sample.path, &error, error.count) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        defer { henge_apk_close(apk) }
        let index = henge_apk_find(apk, "AndroidManifest.xml")
        var bytes: UnsafeMutablePointer<UInt8>?
        var size = 0
        guard index >= 0, henge_apk_extract(apk, index, &bytes, &size, &error, error.count) == 0,
              let bytes else { throw CocoaError(.fileReadCorruptFile) }
        defer { free(bytes) }
        return Data(bytes: bytes, count: size)
    }

    static func main() async throws {
        guard CommandLine.arguments.count == 2 else {
            fatalError("usage: test_library sample.apk")
        }
        let fm = FileManager.default
        let sample = URL(fileURLWithPath: CommandLine.arguments[1]).standardizedFileURL
        let root = fm.temporaryDirectory.appendingPathComponent("henge-library-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: root) }
        let storage = APKStorage(directory: root.appendingPathComponent("library", isDirectory: true))
        check(try storage.files().isEmpty, "a missing library directory is created")

        let metadata = try APKMetadata.read(from: sample)
        check(metadata.package == "dev.henge.sample" && metadata.label == "henge sample", "APK package and literal label")
        check(metadata.versionName == "1.2.3" && metadata.versionCode == 7, "APK version")
        check(metadata.minSDK == 28 && metadata.targetSDK == 35, "APK SDK requirements")
        check(metadata.nativeLibraries == ["libadd.so"] && metadata.architectures == ["arm64-v8a"], "APK native libraries and architectures")
        check(metadata.nativeActivity && metadata.entryLibrary == "libadd.so" && metadata.executionIssue == nil, "NativeActivity entry library is available")

        let first = try storage.importFile(from: sample)
        let second = try storage.importFile(from: sample)
        let third = try storage.importFile(from: sample)
        check(first.lastPathComponent == "sample.apk" && second.lastPathComponent == "sample 2.apk"
              && third.lastPathComponent == "sample 3.apk", "duplicate names do not overwrite imported APKs")
        check(try Data(contentsOf: first) == Data(contentsOf: sample), "import preserves APK contents")

        let invalid = root.appendingPathComponent("invalid.apk")
        try Data("not an APK".utf8).write(to: invalid)
        do {
            try storage.importFile(from: invalid)
            check(false, "invalid APK import is rejected")
        } catch {
            check(true, "invalid APK import is rejected")
        }
        check(try storage.files().count == 3, "a rejected import leaves the library unchanged")
        check(try fm.contentsOfDirectory(atPath: storage.directory.path).allSatisfy { !$0.hasPrefix(".import-") }, "failed import removes its staging copy")

        let binaryManifest = try manifest(in: sample)
        let wrongABI = root.appendingPathComponent("x86.apk")
        try archive(at: wrongABI, entries: ["AndroidManifest.xml": binaryManifest,
                    "lib/x86_64/libadd.so": Data([0]), "classes.dex": Data([0])], in: root)
        let unsupported = try APKMetadata.read(from: wrongABI)
        check(unsupported.architectures == ["x86_64"] && unsupported.hasDex && unsupported.executionIssue != nil,
              "unsupported architecture and bytecode are reported before execution")
        let missingEntry = root.appendingPathComponent("missing-entry.apk")
        try archive(at: missingEntry, entries: ["AndroidManifest.xml": binaryManifest,
                    "lib/arm64-v8a/libother.so": Data([0])], in: root)
        check(try APKMetadata.read(from: missingEntry).executionIssue?.contains("libadd.so") == true,
              "a missing NativeActivity entry library blocks execution")
        let noManifest = root.appendingPathComponent("no-manifest.apk")
        try archive(at: noManifest, entries: ["hello.txt": Data([0])], in: root)
        do {
            try storage.importFile(from: noManifest)
            check(false, "ZIP without AndroidManifest.xml is rejected")
        } catch {
            check(true, "ZIP without AndroidManifest.xml is rejected")
        }

        let oldSource = root.appendingPathComponent("old.apk")
        try fm.copyItem(at: sample, to: oldSource)
        try fm.setAttributes([.creationDate: Date(timeIntervalSince1970: 1)], ofItemAtPath: oldSource.path)
        let before = Date()
        let importedOld = try storage.importFile(from: oldSource)
        let imported = try storage.files().first { $0.url.resolvingSymlinksInPath() == importedOld.resolvingSymlinksInPath() }
        check(imported.map { $0.dateAdded >= before.addingTimeInterval(-1) } == true, "import date reflects this import, not the source file's age")

        try Data([0]).write(to: storage.directory.appendingPathComponent("notes.txt"))
        try fm.createDirectory(at: storage.directory.appendingPathComponent("folder.apk"), withIntermediateDirectories: true)
        check(try storage.files().count == 4, "library ignores non-APK files and directories")

        let files = [
            APKFile(url: root.appendingPathComponent("Game 10.apk"), size: 20, dateAdded: Date(timeIntervalSince1970: 3)),
            APKFile(url: root.appendingPathComponent("game 2.apk"), size: 30, dateAdded: Date(timeIntervalSince1970: 1)),
            APKFile(url: root.appendingPathComponent("Other.apk"), size: 10, dateAdded: Date(timeIntervalSince1970: 2))
        ]
        check(APKSortOrder.name.files(in: files, matching: " GAME ").map(\.name) == ["game 2", "Game 10"], "search ignores case and surrounding whitespace; names sort naturally")
        check(APKSortOrder.newest.files(in: files, matching: "").map(\.size) == [20, 10, 30], "newest-first order")
        check(APKSortOrder.largest.files(in: files, matching: "").map(\.size) == [30, 20, 10], "largest-first order")
        check(APKSortOrder.name.files(in: files, matching: "missing").isEmpty, "a search can have no matches")

        let library = APKLibrary(directory: root.appendingPathComponent("batch", isDirectory: true))
        await library.importFiles(from: [invalid, sample, noManifest])
        check(library.apks.count == 1 && !library.importing, "batch import keeps successful files and ends progress")
        check(library.lastError?.contains("invalid.apk") == true && library.lastError?.contains("no-manifest.apk") == true,
              "batch import reports every failure together")
        await library.importFiles(from: [sample])
        check(library.lastError == nil, "a successful batch clears previous errors")
        let visible = APKSortOrder.name.files(in: library.apks, matching: " 2")
        library.delete(visible)
        check(library.apks.map(\.fileName) == ["sample.apk"], "deleting a filtered selection preserves the other files")
        library.delete(visible)
        check(library.lastError?.contains("Could not delete") == true, "deletion failures are reported")

        let blocked = root.appendingPathComponent("not-a-directory")
        try Data([0]).write(to: blocked)
        let unreadable = APKLibrary(directory: blocked)
        check(unreadable.lastError?.contains("Could not read") == true, "directory errors are visible instead of presenting an empty library silently")

        print("\(failures == 0 ? "PASS" : "FAIL"): test_library (\(failures) failures)")
        if failures != 0 { exit(1) }
    }
}
