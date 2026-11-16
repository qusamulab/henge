import CryptoKit
import Foundation

/// Content-addressed unsigned output is independent of the signing identity. Signed output has
/// its own certificate key and age limit, so changing an identity never re-converts an ELF.
struct APKPreparation {
    struct Symbol: Codable, Hashable {
        let name: String
        let weak: Bool
        let type: UInt32
    }

    struct Library: Codable, Identifiable {
        let name: String
        var soname: String?
        var needed: [String] = []
        var imports: [Symbol] = []
        var exports: [String] = []
        var error: String?
        var digest: String?
        var id: String { name }
        var dylibName: String { String(name.dropLast(3)) + ".dylib" }
    }

    struct Prepared {
        let digest: String
        let directory: URL
        let libraries: [Library]

        var issues: [String] {
            libraries.flatMap { library -> [String] in
                if let error = library.error { return ["\(library.name): \(error)"] }
                var result = library.needed.filter { name in
                    henge_is_system_library(name) == 0 && !libraries.contains { $0.name == name || $0.soname == name }
                }.map { "\(library.name): missing library \($0)" }
                for symbol in unresolved(in: library) where !symbol.weak && symbol.type != 2 {
                    result.append("\(library.name): unresolved data/unknown symbol \(symbol.name)")
                }
                return result
            }
        }

        func unresolved(in library: Library) -> [Symbol] {
            var queue = [library], seen = Set<String>(), available = Set<String>()
            while !queue.isEmpty {
                let next = queue.removeFirst()
                guard seen.insert(next.name).inserted else { continue }
                available.formUnion(next.exports)
                queue += next.needed.compactMap { name in libraries.first { $0.name == name || $0.soname == name } }
            }
            return library.imports.filter { !available.contains($0.name) && henge_shim_provides($0.name) == 0 }
        }
    }

    struct PreparationError: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    static var cacheDirectory: URL {
        FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("henge/artifacts-v4", isDirectory: true)
    }

    // Increment when conversion/patch-island layout or code-signing output changes.
    private static let converterVersion = "converter-4-island-2"
    private static let signingVersion = "cms-2"
    private static let lock = NSLock()

    private static var platform: henge_platform {
        #if os(macOS)
        return HENGE_PLATFORM_MACOS
        #elseif targetEnvironment(simulator)
        return HENGE_PLATFORM_IOSSIM
        #else
        return HENGE_PLATFORM_IOS
        #endif
    }

    static func digest(of url: URL) throws -> String {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        var hash = SHA256()
        while let data = try file.read(upToCount: 1 << 20), !data.isEmpty { hash.update(data: data) }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }

    static func prepare(_ apk: URL, cache: URL = cacheDirectory) throws -> Prepared {
        let digest = try digest(of: apk)
        let key = "\(digest)-\(converterVersion)-\(platform.rawValue)"
        lock.lock()
        defer { lock.unlock() }
        let fm = FileManager.default
        let directory = cache.appendingPathComponent(key, isDirectory: true)
        let report = directory.appendingPathComponent("libraries.json")
        if let data = try? Data(contentsOf: report), let libraries = try? JSONDecoder().decode([Library].self, from: data),
           libraries.allSatisfy({ $0.error != nil || ($0.digest != nil && (try? Self.digest(of: directory.appendingPathComponent($0.dylibName))) == $0.digest) }) {
            return Prepared(digest: key, directory: directory, libraries: libraries)
        }
        try fm.createDirectory(at: cache, withIntermediateDirectories: true)
        let staging = cache.appendingPathComponent(".prepare-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: staging, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: staging) }
        var err = [CChar](repeating: 0, count: 512)
        guard let handle = henge_apk_open(apk.path, &err, err.count) else {
            throw PreparationError(message: String(cString: err))
        }
        defer { henge_apk_close(handle) }
        var indices: UnsafeMutablePointer<Int>?
        var count = 0
        guard henge_apk_native_libs(handle, "arm64-v8a", &indices, &count) == 0 else {
            throw PreparationError(message: "Could not list ARM64 libraries.")
        }
        defer { free(indices) }
        var libraries: [Library] = [], names = Set<String>()
        for index in 0..<count {
            guard let indices, let entry = henge_apk_name(handle, indices[index]) else { continue }
            let name = (String(cString: entry) as NSString).lastPathComponent
            guard names.insert(name).inserted else {
                throw PreparationError(message: "The APK contains duplicate library entries: \(name)")
            }
            var library = Library(name: name)
            let elf = staging.appendingPathComponent("input.so")
            defer { try? fm.removeItem(at: elf) }
            guard henge_apk_extract_file(handle, indices[index], elf.path, &err, err.count) == 0 else {
                library.error = String(cString: err); libraries.append(library); continue
            }
            var result = henge_convert_result()
            defer { henge_convert_result_free(&result) }
            let rc = "@rpath/\(library.dylibName)".withCString { installName in
                var opts = henge_convert_opts(platform: platform, install_name: installName, link_libsystem: 0)
                return henge_convert_file(elf.path, staging.appendingPathComponent(library.dylibName).path,
                                          &opts, &result, &err, err.count)
            }
            guard rc == 0 else {
                library.error = String(cString: err); libraries.append(library); continue
            }
            library.digest = try Self.digest(of: staging.appendingPathComponent(library.dylibName))
            library.imports = (0..<result.nimports).map {
                Symbol(name: String(cString: result.imports[$0].name), weak: result.imports[$0].weak != 0,
                       type: UInt32(result.imports[$0].type))
            }
            library.exports = (0..<result.nexports).map { String(cString: result.exports[$0].name) }
            if let manifest = result.manifest_json,
               let object = try? JSONSerialization.jsonObject(with: Data(String(cString: manifest).utf8)) as? [String: Any] {
                library.soname = object["soname"] as? String
                library.needed = object["needed"] as? [String] ?? []
            }
            libraries.append(library)
        }
        try JSONEncoder().encode(libraries).write(to: staging.appendingPathComponent("libraries.json"), options: .atomic)
        if fm.fileExists(atPath: directory.path) { try fm.removeItem(at: directory) }
        try fm.moveItem(at: staging, to: directory)
        return Prepared(digest: key, directory: directory, libraries: libraries)
    }

    /// On-demand native code uses the same file-backed converter and certificate-keyed signer.
    static func prepareLibrary(_ input: URL, signer: UnsafePointer<henge_signer>?,
                               cache: URL = cacheDirectory) throws -> URL {
        let key = "elf-\(try digest(of: input))-\(converterVersion)-\(platform.rawValue)"
        let directory = cache.appendingPathComponent(key, isDirectory: true)
        let output = directory.appendingPathComponent("library.dylib")
        let prepared: Prepared = try {
            lock.lock(); defer { lock.unlock() }
            let fm = FileManager.default
            try fm.createDirectory(at: directory, withIntermediateDirectories: true)
            let record = directory.appendingPathComponent("digest")
            let expected = try? String(contentsOf: record, encoding: .utf8)
            if expected == nil || (try? digest(of: output)) != expected {
                var result = henge_convert_result()
                defer { henge_convert_result_free(&result) }
                var err = [CChar](repeating: 0, count: 512)
                let rc = "@rpath/library.dylib".withCString { installName in
                    var opts = henge_convert_opts(platform: platform, install_name: installName, link_libsystem: 0)
                    return henge_convert_file(input.path, output.path, &opts, &result, &err, err.count)
                }
                guard rc == 0 else { throw PreparationError(message: String(cString: err)) }
                try digest(of: output).write(to: record, atomically: true, encoding: .utf8)
            }
            return Prepared(digest: key, directory: directory, libraries: [Library(name: "library.so")])
        }()
        return try sign(prepared, signer: signer, cache: cache).appendingPathComponent("library.dylib")
    }

    static func sign(_ prepared: Prepared, signer: UnsafePointer<henge_signer>?,
                     cache: URL = cacheDirectory) throws -> URL {
        let fingerprint: String
        if let signer {
            fingerprint = SHA256.hash(data: Data(bytes: signer.pointee.cert, count: signer.pointee.cert_len))
                .map { String(format: "%02x", $0) }.joined()
        } else { fingerprint = "adhoc" }
        lock.lock()
        defer { lock.unlock() }
        let fm = FileManager.default
        let signed = cache.appendingPathComponent("\(prepared.digest)-\(signingVersion)-\(fingerprint)", isDirectory: true)
        let stamp = signed.appendingPathComponent("signed-at")
        // A short TTL prevents indefinite reuse of signatures made with short-lived identities.
        if let attrs = try? fm.attributesOfItem(atPath: stamp.path), let date = attrs[.modificationDate] as? Date,
           (0..<86400).contains(Date().timeIntervalSince(date)),
           let record = try? Data(contentsOf: signed.appendingPathComponent("signed-files.json")),
           let hashes = try? JSONDecoder().decode([String: String].self, from: record),
           prepared.libraries.allSatisfy({ $0.error != nil || (hashes[$0.dylibName] != nil && (try? Self.digest(of: signed.appendingPathComponent($0.dylibName))) == hashes[$0.dylibName]) }) {
            return signed
        }
        let staging = cache.appendingPathComponent(".sign-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: staging, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: staging) }
        var err = [CChar](repeating: 0, count: 512)
        var hashes: [String: String] = [:]
        for library in prepared.libraries where library.error == nil {
            let input = prepared.directory.appendingPathComponent(library.dylibName)
            let output = staging.appendingPathComponent(library.dylibName)
            guard henge_sign_file(input.path, output.path, String(library.name.dropLast(3)), signer, &err, err.count) == 0 else {
                throw PreparationError(message: "Sign \(library.name): \(String(cString: err))")
            }
            hashes[library.dylibName] = try digest(of: output)
        }
        try JSONEncoder().encode(hashes).write(to: staging.appendingPathComponent("signed-files.json"))
        try Data().write(to: staging.appendingPathComponent("signed-at"))
        if fm.fileExists(atPath: signed.path) { try fm.removeItem(at: signed) }
        try fm.moveItem(at: staging, to: signed)
        return signed
    }
}
