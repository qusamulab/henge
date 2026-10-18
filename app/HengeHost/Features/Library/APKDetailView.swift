import SwiftUI

struct APKDetailView: View {
    let apk: APKFile
    @EnvironmentObject private var converter: ConvertRunner

    var body: some View {
        List {
            Section {
                VStack(spacing: 10) {
                    Image(systemName: "shippingbox.fill")
                        .font(.system(size: 52))
                        .foregroundStyle(.green)
                        .accessibilityHidden(true)
                    Text(apk.name)
                        .font(.title2.bold())
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 8)
                .listRowBackground(Color.clear)
            }

            Section("File") {
                LabeledContent("Name", value: apk.fileName)
                LabeledContent("Size", value: apk.size.formatted(.byteCount(style: .file)))
                LabeledContent("Imported", value: apk.dateAdded.formatted(date: .long, time: .shortened))
                LabeledContent("Location") {
                    Text("Application Support/henge/apks")
                        .font(.footnote.monospaced())
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.trailing)
                }
            }

            Section {
                Button {
                    converter.run(apk: apk.url)
                } label: {
                    Label(converter.state == .running ? "Running\u{2026}" : "Convert & Run", systemImage: "play.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .disabled(converter.state == .running)
                .listRowInsets(EdgeInsets())
                .listRowBackground(Color.clear)
            } footer: {
                Text("Extracts lib/arm64-v8a/*.so from the APK, converts and signs it, then loads it.")
                    .frame(maxWidth: .infinity)
                    .multilineTextAlignment(.center)
                    .padding(.top, 4)
            }

            if converter.state != .idle {
                Section("Result") {
                    Text(converter.logText)
                        .font(.caption.monospaced())
                        .textSelection(.enabled)
                }
            }
        }
        .navigationTitle(apk.name)
        .navigationBarTitleDisplayMode(.inline)
    }
}
