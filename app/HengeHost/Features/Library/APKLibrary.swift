import Foundation
import Combine
import os

/// Main-actor state for the library; copying and validating APKs happens in a background task.
@MainActor
final class APKLibrary: ObservableObject {
    @Published private(set) var apks: [APKFile] = []
    @Published private(set) var importing = false
    @Published var lastError: String?

    private let log = Logger(subsystem: Bundle.main.bundleIdentifier ?? "henge", category: "library")
    private let storage: APKStorage
    var directory: URL { storage.directory }

    init(directory: URL = APKStorage.applicationDirectory) {
        storage = APKStorage(directory: directory)
        reload()
    }

    func reload() {
        do {
            apks = try storage.files()
        } catch {
            lastError = "Could not read the library: \(error.localizedDescription)"
        }
    }

    /// Report every failed selection together, while keeping successfully imported APKs.
    func importFiles(from sources: [URL]) async {
        guard !importing else { return }
        importing = true
        defer { importing = false }
        let storage = storage
        let errors = await Task.detached(priority: .userInitiated) {
            var errors: [String] = []
            for source in sources {
                do {
                    try storage.importFile(from: source)
                } catch {
                    errors.append("\(source.lastPathComponent): \(error.localizedDescription)")
                }
            }
            return errors
        }.value
        reload()
        if !errors.isEmpty { lastError = "Could not import:\n" + errors.joined(separator: "\n") }
        log.log("import finished: \(sources.count - errors.count) of \(sources.count) files")
    }

    func delete(_ files: [APKFile]) {
        var errors: [String] = []
        for apk in files {
            do {
                try storage.delete(apk)
            } catch {
                errors.append("\(apk.fileName): \(error.localizedDescription)")
            }
        }
        reload()
        if !errors.isEmpty { lastError = "Could not delete:\n" + errors.joined(separator: "\n") }
    }
}
