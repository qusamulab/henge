import SwiftUI

struct LibraryView: View {
    @EnvironmentObject private var library: APKLibrary
    @State private var showingImporter = false
    @State private var searchText = ""
    @AppStorage("library.sortOrder") private var sortOrder = APKSortOrder.newest.rawValue

    private var visibleAPKs: [APKFile] {
        (APKSortOrder(rawValue: sortOrder) ?? .newest).files(in: library.apks, matching: searchText)
    }

    var body: some View {
        NavigationStack {
            Group {
                if library.apks.isEmpty {
                    EmptyStateView(
                        title: "No APKs Yet",
                        systemImage: "shippingbox",
                        message: "Import an Android APK to inspect it and run its ARM64 native code on iPhone."
                    ) {
                        Button {
                            showingImporter = true
                        } label: {
                            Label("Import APK", systemImage: "square.and.arrow.down")
                        }
                        .buttonStyle(.borderedProminent)
                        .controlSize(.large)
                        .disabled(library.importing)
                    }
                } else if visibleAPKs.isEmpty {
                    EmptyStateView(
                        title: "No Matching APKs",
                        systemImage: "magnifyingglass",
                        message: "Try a different file name or clear the search."
                    ) {
                        Button("Clear Search") { searchText = "" }
                            .buttonStyle(.bordered)
                    }
                } else {
                    let files = visibleAPKs
                    List {
                        ForEach(files) { apk in
                            NavigationLink(value: apk) {
                                APKRow(apk: apk)
                            }
                        }
                        .onDelete { offsets in
                            library.delete(offsets.map { files[$0] })
                        }
                    }
                    .refreshable { library.reload() }
                }
            }
            .searchable(text: $searchText, prompt: "Search APKs")
            .navigationTitle("Library")
            .navigationDestination(for: APKFile.self) { APKDetailView(apk: $0) }
            .toolbar {
                ToolbarItemGroup(placement: .navigationBarTrailing) {
                    if !library.apks.isEmpty {
                        Menu {
                            Picker("Sort APKs", selection: $sortOrder) {
                                ForEach(APKSortOrder.allCases, id: \.rawValue) { order in
                                    Text(order.title).tag(order.rawValue)
                                }
                            }
                        } label: {
                            Label("Sort APKs", systemImage: "arrow.up.arrow.down")
                        }
                        Button {
                            showingImporter = true
                        } label: {
                            Label("Import APK", systemImage: "plus")
                        }
                        .disabled(library.importing)
                    }
                }
            }
            .safeAreaInset(edge: .bottom) {
                if library.importing {
                    HStack(spacing: 10) {
                        ProgressView()
                        Text("Importing APKs…").font(.callout)
                    }
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(.bar)
                }
            }
            .fileImporter(isPresented: $showingImporter, allowedContentTypes: [.apk],
                          allowsMultipleSelection: true) { result in
                switch result {
                case .success(let urls): Task { await library.importFiles(from: urls) }
                case .failure(let error): library.lastError = error.localizedDescription
                }
            }
            .alert("Library Error", isPresented: Binding(
                get: { library.lastError != nil },
                set: { if !$0 { library.lastError = nil } }
            )) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(library.lastError ?? "")
            }
            .onAppear { library.reload() }
        }
    }
}

private struct APKRow: View {
    let apk: APKFile

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "shippingbox.fill")
                .font(.title2)
                .foregroundStyle(.green)
                .frame(width: 36)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 2) {
                Text(apk.name)
                    .font(.headline)
                    .lineLimit(1)
                Text("\(apk.size.formatted(.byteCount(style: .file))) · \(apk.dateAdded.formatted(date: .abbreviated, time: .shortened))")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 2)
    }
}
