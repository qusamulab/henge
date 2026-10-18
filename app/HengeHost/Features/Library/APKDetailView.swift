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
                if converter.state == .active {
                    Button(role: .destructive) {
                        converter.stop()
                    } label: {
                        Label("Stop", systemImage: "stop.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .listRowInsets(EdgeInsets())
                    .listRowBackground(Color.clear)
                } else {
                    Button {
                        converter.run(apk: apk.url, display: true)
                    } label: {
                        Label(converter.state == .running ? "Running\u{2026}" : "Convert & Run", systemImage: "play.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(converter.state == .running)
                    .listRowInsets(EdgeInsets())
                    .listRowBackground(Color.clear)
                }
            } footer: {
                Text(converter.state == .active
                     ? "The app's android_main is running. There is no display yet, so its log lines are shown below."
                     : "Extracts lib/arm64-v8a/*.so from the APK, converts and signs it, then loads and starts it.")
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
