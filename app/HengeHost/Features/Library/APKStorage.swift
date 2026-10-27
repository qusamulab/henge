import Foundation

struct APKFile: Identifiable, Hashable, Sendable {
    let url: URL
    let size: Int64
    let dateAdded: Date
    var id: URL { url }
    var name: String { url.deletingPathExtension().lastPathComponent }
    var fileName: String { url.lastPathComponent }
}

enum APKSortOrder: String, CaseIterable {
    case newest, name, largest

    var title: String {
        switch self {
        case .newest: return "Newest First"
        case .name: return "Name"
        case .largest: return "Largest First"
        }
    }

    func files(in files: [APKFile], matching query: String) -> [APKFile] {
        let query = query.trimmingCharacters(in: .whitespacesAndNewlines)
        return files.filter { query.isEmpty || $0.fileName.localizedStandardContains(query) }
            .sorted { lhs, rhs in
                switch self {
                case .newest where lhs.dateAdded != rhs.dateAdded: return lhs.dateAdded > rhs.dateAdded
                case .largest where lhs.size != rhs.size: return lhs.size > rhs.size
                default:
                    let order = lhs.fileName.localizedStandardCompare(rhs.fileName)
                    return order == .orderedSame ? lhs.url.path < rhs.url.path : order == .orderedAscending
                }
            }
    }
}

/// File operations independent of SwiftUI and its main-actor state. The directory is injectable.
struct APKStorage: Sendable {
    let directory: URL

    static var applicationDirectory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("henge/apks", isDirectory: true)
    }

    func files() throws -> [APKFile] {
        let fm = FileManager.default
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let keys: Set<URLResourceKey> = [.fileSizeKey, .creationDateKey, .isRegularFileKey]
        let urls = try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: Array(keys),
                                             options: [.skipsHiddenFiles])
        let files = try urls.filter { $0.pathExtension.lowercased() == "apk" }.compactMap { url -> APKFile? in
            let values = try url.resourceValues(forKeys: keys)
            guard values.isRegularFile == true else { return nil }
            return APKFile(url: url, size: Int64(values.fileSize ?? 0),
                           dateAdded: values.creationDate ?? .distantPast)
        }
        return APKSortOrder.newest.files(in: files, matching: "")
    }

    /// Validate a hidden staging copy before publishing it. Failed imports leave no library entry.
    @discardableResult
    func importFile(from source: URL) throws -> URL {
        let fm = FileManager.default
        let scoped = source.startAccessingSecurityScopedResource()
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }
        guard source.pathExtension.lowercased() == "apk" else {
            throw CocoaError(.fileReadCorruptFile)
        }
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let staging = directory.appendingPathComponent(".import-\(UUID().uuidString).apk")
        defer { try? fm.removeItem(at: staging) }
        try fm.copyItem(at: source, to: staging)
        _ = try APKMetadata.read(from: staging)
        // A copied file can retain its original creation date; library order records this import.
        try fm.setAttributes([.creationDate: Date()], ofItemAtPath: staging.path)
        let destination = uniqueDestination(for: source.lastPathComponent)
        try fm.moveItem(at: staging, to: destination)
        return destination
    }

    func delete(_ apk: APKFile) throws {
        try FileManager.default.removeItem(at: apk.url)
    }

    private func uniqueDestination(for fileName: String) -> URL {
        let original = directory.appendingPathComponent(fileName)
        var destination = original
        var suffix = 2
        while FileManager.default.fileExists(atPath: destination.path) {
            destination = directory.appendingPathComponent("\(original.deletingPathExtension().lastPathComponent) \(suffix)")
                .appendingPathExtension(original.pathExtension)
            suffix += 1
        }
        return destination
    }
}
