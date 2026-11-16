import SwiftUI

struct APKDetailView: View {
    let apk: APKFile
    @EnvironmentObject private var converter: ConvertRunner
    @State private var metadata: APKMetadata?
    @State private var inspectionError: String?
    @State private var preparation: APKPreparation.Prepared?
    @State private var preparationError: String?

    private var isCurrentRun: Bool { converter.apkURL == apk.url.resolvingSymlinksInPath().standardizedFileURL }
    private var runnerBusy: Bool {
        converter.state == .running || converter.state == .active || converter.state == .awaitingSurface
    }

    var body: some View {
        List {
            Section {
                VStack(spacing: 10) {
                    Image(systemName: "shippingbox.fill")
                        .font(.system(size: 52))
                        .foregroundStyle(.green)
                        .accessibilityHidden(true)
                    Text(metadata.flatMap { $0.label.isEmpty ? nil : $0.label } ?? apk.name)
                        .font(.title2.bold())
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 8)
                .listRowBackground(Color.clear)
            }

            Section {
                if isCurrentRun && (converter.state == .active || converter.state == .awaitingSurface) {
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
                        Label(converter.requiresRestart && !runnerBusy ? "Restart Required" : isCurrentRun && converter.state == .running ? "Running…" : "Convert & Run", systemImage: "play.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(runnerBusy || converter.requiresRestart || metadata == nil || metadata?.executionIssue != nil
                              || preparation == nil || preparation?.issues.isEmpty == false || preparationError != nil)
                    .listRowInsets(EdgeInsets())
                    .listRowBackground(Color.clear)
                }
            } footer: {
                Text(converter.requiresRestart && !runnerBusy
                     ? "Close henge from the app switcher and open it again before running another APK."
                     : runnerBusy
                     ? (isCurrentRun ? "This APK is running. Close the game screen or use Stop to end the session."
                        : "Another APK is running. Stop it before starting this one.")
                     : "Converts and signs the ARM64 native libraries, then loads them. NativeActivity apps open a game screen.")
                    .frame(maxWidth: .infinity)
                    .multilineTextAlignment(.center)
                    .padding(.top, 4)
            }

            Section("APK Information") {
                if let metadata {
                    LabeledContent("Package", value: metadata.package)
                    LabeledContent("Version", value: "\(metadata.versionName.isEmpty ? "—" : metadata.versionName) (\(metadata.versionCode))")
                    LabeledContent("Minimum SDK", value: metadata.minSDK > 0 ? "\(metadata.minSDK)" : "Not specified")
                    LabeledContent("Target SDK", value: metadata.targetSDK > 0 ? "\(metadata.targetSDK)" : "Not specified")
                    LabeledContent("Architectures", value: metadata.architectures.isEmpty ? "No native libraries" : metadata.architectures.joined(separator: ", "))
                    if !metadata.launcherActivity.isEmpty {
                        LabeledContent("Launcher", value: metadata.launcherActivity)
                    }
                    if metadata.nativeActivity {
                        LabeledContent("NativeActivity", value: metadata.entryLibrary)
                    }
                } else if let inspectionError {
                    Label(inspectionError, systemImage: "exclamationmark.triangle")
                        .foregroundStyle(.secondary)
                    Button("Retry Inspection") { Task { await inspect() } }
                } else {
                    ProgressView("Reading APK information…")
                }
            }

            if let metadata {
                Section("Execution") {
                    if let issue = metadata.executionIssue {
                        Label(issue, systemImage: "exclamationmark.triangle")
                    } else {
                        Label(metadata.nativeLibraries.isEmpty ? "Java launcher found" : "ARM64 native libraries found", systemImage: "checkmark.circle")
                        Text("Execution is experimental. Other Android APIs or dependencies may still be unsupported.")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    if metadata.hasDex {
                        Text("This APK contains Java/Kotlin bytecode. Java activities use the experimental henge-vm and its JNI bridge. Android framework coverage is limited.")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
                if !metadata.nativeLibraries.isEmpty {
                    Section("ARM64 Libraries") {
                        ForEach(metadata.nativeLibraries, id: \.self) { name in
                            Text(name).font(.footnote.monospaced())
                        }
                    }
                }
            }

            if let preparation {
                Section("Native Compatibility") {
                    ForEach(preparation.libraries) { library in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(library.name).font(.footnote.monospaced())
                            if let error = library.error {
                                Text(error).foregroundStyle(.red)
                            } else {
                                let missing = preparation.unresolved(in: library)
                                Text("\(library.imports.count) imports, \(missing.count) unresolved")
                                    .foregroundStyle(.secondary)
                                ForEach(missing, id: \.self) { symbol in
                                    Text("\(symbol.name)\(symbol.weak ? " (weak)" : symbol.type == 2 ? " (stops if called)" : " (blocks loading)")")
                                        .font(.caption.monospaced())
                                }
                            }
                        }
                    }
                    ForEach(preparation.issues, id: \.self) { Text($0).foregroundStyle(.red) }
                    Text("Static checks cannot establish runtime compatibility. Unsupported functions stop the app if called.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
            } else if let preparationError {
                Section("Native Compatibility") { Text(preparationError) }
            } else if metadata != nil {
                Section("Native Compatibility") { ProgressView("Checking native libraries…") }
            }

            if let crash = converter.previousCrash {
                Section("Previous Crash") {
                    Text(crash).font(.caption.monospaced()).textSelection(.enabled)
                    ShareLink(item: crash) { Label("Share Crash", systemImage: "square.and.arrow.up") }
                }
            }

            Section("File") {
                LabeledContent("Name", value: apk.fileName)
                LabeledContent("Size", value: apk.size.formatted(.byteCount(style: .file)))
                LabeledContent("Imported", value: apk.dateAdded.formatted(date: .long, time: .shortened))
            }

            if isCurrentRun && converter.state != .idle {
                Section("Result") {
                    Text(converter.logText)
                        .font(.caption.monospaced())
                        .textSelection(.enabled)
                    ShareLink(item: converter.logText) {
                        Label("Share Log", systemImage: "square.and.arrow.up")
                    }
                }
            }
        }
        .navigationTitle(apk.name)
        .navigationBarTitleDisplayMode(.inline)
        .task(id: apk.id) { await inspect() }
    }

    @MainActor
    private func inspect() async {
        converter.refreshProcessState()
        metadata = nil
        inspectionError = nil
        preparation = nil
        preparationError = nil
        let url = apk.url
        let result = await Task.detached(priority: .userInitiated) {
            Result { try APKMetadata.read(from: url) }
        }.value
        guard !Task.isCancelled else { return }
        switch result {
        case .success(let value): metadata = value
        case .failure(let error): inspectionError = error.localizedDescription
        }
        guard metadata != nil else { return }
        let prepared = await Task.detached(priority: .userInitiated) {
            Result { try APKPreparation.prepare(url) }
        }.value
        guard !Task.isCancelled else { return }
        switch prepared {
        case .success(let value): preparation = value
        case .failure(let error): preparationError = error.localizedDescription
        }
    }
}
