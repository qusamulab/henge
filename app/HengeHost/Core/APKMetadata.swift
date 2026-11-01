import Foundation

/// A value snapshot of the APK reader's C buffers, shared by import, inspection and execution.
struct APKMetadata: Hashable, Sendable {
    let package: String
    let label: String
    let versionName: String
    let versionCode: Int64
    let minSDK: Int
    let targetSDK: Int
    let nativeActivity: Bool
    let libraryName: String
    let launcherActivity: String
    let nativeLibraries: [String]
    let architectures: [String]
    let hasDex: Bool

    var entryLibrary: String { "lib\(libraryName).so" }

    /// These checks only establish whether the host can attempt execution, not full compatibility.
    var executionIssue: String? {
        if nativeLibraries.isEmpty {
            return "This host needs ARM64 native libraries (lib/arm64-v8a/*.so). "
                + "Java/Kotlin activities cannot run in the iOS host yet."
        }
        if nativeActivity && !nativeLibraries.contains(entryLibrary) {
            return "The NativeActivity entry library \(entryLibrary) is missing for arm64-v8a."
        }
        return nil
    }

    static func read(from url: URL) throws -> APKMetadata {
        var error = [CChar](repeating: 0, count: 512)
        guard let handle = henge_apk_open(url.path, &error, error.count) else {
            throw APKReadError(message: String(cString: error))
        }
        defer { henge_apk_close(handle) }
        return try APKMetadata(reading: handle)
    }

    init(reading handle: OpaquePointer) throws {
        var error = [CChar](repeating: 0, count: 512)
        var manifest = henge_apk_manifest()
        guard henge_apk_read_manifest(handle, &manifest, &error, error.count) == 0 else {
            throw APKReadError(message: String(cString: error))
        }
        package = Self.string(manifest.package)
        guard !package.isEmpty, package != ".", package != "..",
              !package.contains("/"), !package.contains("\\") else {
            throw APKReadError(message: "The manifest package is not a valid directory name.")
        }
        label = Self.string(manifest.label)
        versionName = Self.string(manifest.version_name)
        versionCode = manifest.version_code
        minSDK = Int(manifest.min_sdk)
        targetSDK = Int(manifest.target_sdk)
        nativeActivity = manifest.native_activity != 0
        libraryName = Self.string(manifest.lib_name)
        launcherActivity = Self.string(manifest.launcher_activity)

        var indices: UnsafeMutablePointer<Int>?
        var count = 0
        guard henge_apk_native_libs(handle, "arm64-v8a", &indices, &count) == 0 else {
            throw APKReadError(message: "Could not read native libraries: out of memory.")
        }
        defer { free(indices) }
        nativeLibraries = (0..<count).compactMap { index in
            guard let indices, let name = henge_apk_name(handle, indices[index]) else { return nil }
            return (String(cString: name) as NSString).lastPathComponent
        }.sorted()

        var abis = Set<String>()
        for index in 0..<henge_apk_count(handle) {
            guard let name = henge_apk_name(handle, index) else { continue }
            let parts = String(cString: name).split(separator: "/", omittingEmptySubsequences: false)
            if parts.count == 3, parts[0] == "lib", !parts[1].isEmpty,
               parts[2].hasSuffix(".so"), parts[2].count > 3 {
                abis.insert(String(parts[1]))
            }
        }
        architectures = abis.sorted()
        hasDex = henge_apk_find(handle, "classes.dex") >= 0
    }

    /// Imported fixed-size C character arrays are Swift tuples. Decode within their bounds.
    private static func string<T>(_ buffer: T) -> String {
        withUnsafeBytes(of: buffer) { bytes in
            String(decoding: bytes.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
    }
}

private struct APKReadError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}
