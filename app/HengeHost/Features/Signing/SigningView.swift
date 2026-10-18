import SwiftUI
import UniformTypeIdentifiers

/// Import the developer identity (.p12) used to sign converted libraries.
struct SigningView: View {
    @State private var summary = SigningStore.storedSummary()
    @State private var importing = false
    @State private var pending: Data?
    @State private var password = ""
    @State private var error: String?

    var body: some View {
        NavigationStack {
            List {
                Section {
                    if let summary {
                        Label(summary, systemImage: "checkmark.seal.fill").foregroundStyle(.green)
                    } else {
                        Label("No identity", systemImage: "exclamationmark.triangle").foregroundStyle(.orange)
                    }
                } header: {
                    Text("Signing identity")
                } footer: {
                    Text("iOS only loads libraries from the app container if they are signed by your developer team. "
                        + "Export your Apple Development identity from Keychain Access as a .p12 and import it here. "
                        + "It stays in this device's Keychain.")
                }
                Section {
                    Button { importing = true } label: { Label("Import .p12", systemImage: "square.and.arrow.down") }
                    if summary != nil {
                        Button(role: .destructive) {
                            SigningStore.remove()
                            summary = nil
                        } label: { Label("Remove identity", systemImage: "trash") }
                    }
                }
            }
            .navigationTitle("Signing")
            .onAppear { if summary != nil { SigningStore.removeImportedCopies() } }
            .fileImporter(isPresented: $importing, allowedContentTypes: [.pkcs12]) { result in
                switch result {
                case .success(let url):
                    let scoped = url.startAccessingSecurityScopedResource()
                    defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                    do { pending = try Data(contentsOf: url) } catch { self.error = error.localizedDescription }
                case .failure(let e): error = e.localizedDescription
                }
            }
            .alert("Password", isPresented: Binding(get: { pending != nil }, set: { if !$0 { pending = nil; password = "" } })) {
                SecureField("Password of the .p12", text: $password)
                Button("Import") { save() }
                Button("Cancel", role: .cancel) {}
            }
            .alert("Import Failed", isPresented: Binding(get: { error != nil }, set: { if !$0 { error = nil } })) {
                Button("OK", role: .cancel) {}
            } message: { Text(error ?? "") }
        }
    }

    private func save() {
        guard let data = pending else { return }
        defer { pending = nil; password = "" }
        do {
            try SigningStore.save(p12: data, password: password)
            summary = SigningStore.storedSummary()
        } catch {
            self.error = error.localizedDescription
        }
    }
}
