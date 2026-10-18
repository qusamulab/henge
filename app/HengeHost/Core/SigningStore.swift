import Foundation
import Security

/// The developer identity (.p12) henge signs converted libraries with. iOS only loads libraries from the
/// app container when they carry a signature from the app's own team, so henge signs on the device.
/// The .p12 and its password live in the Keychain (this device only, after first unlock).
enum SigningStore {
    private static let service = "henge.signing"

    enum StoreError: LocalizedError {
        case invalid(String)
        case keychain(OSStatus)
        var errorDescription: String? {
            switch self {
            case .invalid(let m): return m
            case .keychain(let s): return "Keychain error \(s)"
            }
        }
    }

    /// Checks the .p12 and password, then stores both.
    static func save(p12: Data, password: String) throws {
        guard let signer = makeSigner(p12: p12, password: password) else {
            throw StoreError.invalid(lastError)
        }
        henge_signer_free(signer.pointer)
        try set(account: "p12", data: p12)
        try set(account: "password", data: Data(password.utf8))
    }

    /// Deletes .p12 files left in the app's Documents folder (the import picker's source) once the identity is
    /// stored in the Keychain.
    static func removeImportedCopies() {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let files = (try? FileManager.default.contentsOfDirectory(at: docs, includingPropertiesForKeys: nil)) ?? []
        for f in files where f.pathExtension.lowercased() == "p12" { try? FileManager.default.removeItem(at: f) }
    }

    static func remove() {
        for account in ["p12", "password"] {
            SecItemDelete(query(account) as CFDictionary)
        }
    }

    /// A signer for the stored identity, or nil. Free it with `Signer.free()`.
    static func loadSigner() -> Signer? {
        guard let p12 = get(account: "p12"), let pw = get(account: "password") else { return nil }
        return makeSigner(p12: p12, password: String(decoding: pw, as: UTF8.self))
    }

    /// Owner of a henge_signer plus a description of its certificate.
    struct Signer {
        let pointer: UnsafeMutablePointer<henge_signer>
        let summary: String
        func free() { henge_signer_free(pointer) }
    }

    private static var lastError = ""

    private static func makeSigner(p12: Data, password: String) -> Signer? {
        var err = [CChar](repeating: 0, count: 512)
        let signer: UnsafeMutablePointer<henge_signer>? = p12.withUnsafeBytes { raw in
            henge_signer_from_pkcs12(raw.bindMemory(to: UInt8.self).baseAddress, p12.count, password, &err, err.count)
        }
        guard let signer else {
            lastError = String(cString: err)
            return nil
        }
        let cert = Data(bytes: signer.pointee.cert, count: signer.pointee.cert_len)
        let name = SecCertificateCreateWithData(nil, cert as CFData).flatMap { SecCertificateCopySubjectSummary($0) as String? }
        return Signer(pointer: signer, summary: name ?? "unknown certificate")
    }

    /// Summary of the stored identity without keeping a signer around.
    static func storedSummary() -> String? {
        guard let s = loadSigner() else { return nil }
        defer { s.free() }
        return s.summary
    }

    // MARK: Keychain

    private static func query(_ account: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: account]
    }

    private static func get(account: String) -> Data? {
        var q = query(account)
        q[kSecReturnData as String] = true
        var out: CFTypeRef?
        return SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess ? out as? Data : nil
    }

    private static func set(account: String, data: Data) throws {
        SecItemDelete(query(account) as CFDictionary)
        var q = query(account)
        q[kSecValueData as String] = data
        q[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        let st = SecItemAdd(q as CFDictionary, nil)
        if st != errSecSuccess { throw StoreError.keychain(st) }
    }
}
