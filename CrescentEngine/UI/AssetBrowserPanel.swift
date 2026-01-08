import SwiftUI
import UniformTypeIdentifiers
import AppKit

struct AssetBrowserPanel: View {
    @ObservedObject var editorState: EditorState
    @State private var searchText: String = ""
    @State private var selectedFilter: AssetFilter = .all
    @State private var selectedFolderURL: URL?
    @State private var folderTree: FolderNode?
    @State private var expandedFolders: Set<String> = []
    @State private var gridSize: CGFloat = 160
    @State private var isDropping: Bool = false
    @State private var selectedAssetID: UUID?
    @State private var viewMode: AssetViewMode = .grid
    @State private var selectedAssetGuid: String = ""
    @State private var selectedModelOptions = ModelImportOptions()
    @State private var selectedTextureOptions = TextureImportOptions()
    @State private var selectedHdriOptions = HdriImportOptions()
    
    private let modelExtensions = [
        "fbx", "obj", "gltf", "glb", "dae", "blend", "3ds",
        "stl", "ply", "x", "smd", "md5mesh", "md2", "md3", "ms3d", "lwo", "lws"
    ]
    
    private let textureExtensions = [
        "png", "jpg", "jpeg", "tga", "bmp", "gif", "tif", "tiff", "ktx", "ktx2", "dds", "cube"
    ]
    
    private let hdriExtensions = [
        "hdr", "exr"
    ]
    
    private var projectRootURL: URL? {
        editorState.projectRootURL ?? editorState.assetRootURL
    }
    
    private var assetsRootURL: URL? {
        editorState.projectAssetsURL ?? editorState.assetRootURL
    }

    private var scenesRootURL: URL? {
        editorState.projectScenesURL
    }
    
    private var baseFolderURL: URL? {
        if let selected = selectedFolderURL {
            let selectedPath = selected.standardizedFileURL.path
            if let assetsRoot = assetsRootURL {
                let assetsPath = assetsRoot.standardizedFileURL.path
                if selectedPath == assetsPath || selectedPath.hasPrefix(assetsPath + "/") {
                    return selected
                }
            }
            if let scenesRoot = scenesRootURL {
                let scenesPath = scenesRoot.standardizedFileURL.path
                if selectedPath == scenesPath || selectedPath.hasPrefix(scenesPath + "/") {
                    return selected
                }
            }
            if let projectRoot = projectRootURL {
                let projectPath = projectRoot.standardizedFileURL.path
                if selectedPath == projectPath {
                    return projectRoot
                }
            }
            return nil
        }
        return assetsRootURL ?? projectRootURL
    }
    
    private var filteredAssets: [AssetInfo] {
        guard let baseFolder = baseFolderURL else {
            return []
        }
        var list = editorState.assets
        let trimmed = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        
        let folderPath = baseFolder.standardizedFileURL.path
        list = list.filter { $0.path.hasPrefix(folderPath) }
        
        if selectedFilter != .all {
            list = list.filter { $0.type == selectedFilter.assetType }
        }
        
        if !trimmed.isEmpty {
            list = list.filter {
                $0.name.localizedCaseInsensitiveContains(trimmed) ||
                $0.relativePath.localizedCaseInsensitiveContains(trimmed)
            }
        }
        
        return list.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    private var selectedAsset: AssetInfo? {
        guard let selectedAssetID else { return nil }
        return editorState.assets.first { $0.id == selectedAssetID }
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 8) {
                Label("Explorer", systemImage: "folder.fill")
                    .labelStyle(.titleAndIcon)
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
                
                Spacer()

                Toggle(isOn: $editorState.autoReimportAssets) {
                    Text("Auto")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(EditorTheme.textMuted)
                }
                .toggleStyle(.switch)
                .controlSize(.small)
                .help("Auto reimport assets on disk changes")

                Button(action: {
                    openImporter()
                }) {
                    Image(systemName: "square.and.arrow.down")
                        .font(EditorTheme.fontBodyMedium)
                        .foregroundColor(EditorTheme.textPrimary)
                }
                .buttonStyle(.borderless)
                .help("Import Assets")
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(EditorTheme.panelHeader)
            
            Divider()
                .overlay(EditorTheme.panelStroke)
            
            HStack(alignment: .top, spacing: 12) {
                AssetFolderSidebar(
                    folderTree: folderTree,
                    selectedFolderURL: $selectedFolderURL,
                    expandedFolders: $expandedFolders,
                    editorState: editorState,
                    onRescan: editorState.rescanAssetRoot,
                    onSelectRoot: { editorState.pickAssetRoot() }
                )
                .frame(minWidth: 220, idealWidth: 240, maxWidth: 280)
                .frame(maxHeight: .infinity)
                
                VStack(alignment: .leading, spacing: 12) {
                    AssetGridPanel(
                        assets: filteredAssets,
                        gridSize: $gridSize,
                        viewMode: $viewMode,
                        selectedFilter: $selectedFilter,
                        searchText: $searchText,
                        selectedAssetID: $selectedAssetID,
                        isDropping: $isDropping,
                        assetRootURL: assetsRootURL,
                        onDrop: handleDrop,
                        onOpenScene: openSceneAsset,
                        onReveal: revealInFinder,
                        onRename: promptRename
                    )
                    .frame(maxWidth: .infinity, maxHeight: .infinity)

                    if let selectedAsset = selectedAsset, !selectedAssetGuid.isEmpty {
                        AssetImportSettingsPanel(
                            asset: selectedAsset,
                            modelOptions: $selectedModelOptions,
                            textureOptions: $selectedTextureOptions,
                            hdriOptions: $selectedHdriOptions,
                            onApply: applySelectedAssetSettings,
                            onReimport: reimportSelectedAsset
                        )
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .frame(maxHeight: .infinity, alignment: .top)
            .padding(12)
        }
        .frame(minHeight: 180)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
                .allowsHitTesting(false)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .clipped()
        .onAppear {
            reloadFolderTree()
        }
        .onChange(of: editorState.assetRootURL) { _ in
            reloadFolderTree()
        }
        .onChange(of: editorState.projectRootURL) { _ in
            reloadFolderTree()
        }
        .onChange(of: selectedAssetID) { _ in
            loadSelectedAssetSettings()
        }
        .onChange(of: editorState.assets) { _ in
            loadSelectedAssetSettings()
        }
    }
    
    private func reloadFolderTree() {
        guard let root = projectRootURL ?? assetsRootURL else {
            folderTree = nil
            selectedFolderURL = nil
            expandedFolders = []
            return
        }
        folderTree = buildFolderTree(root)
        if let projectRoot = projectRootURL {
            selectedFolderURL = projectRoot
        } else {
            selectedFolderURL = assetsRootURL ?? root
        }
        var expanded: Set<String> = [root.standardizedFileURL.path]
        if let assets = assetsRootURL {
            expanded.insert(assets.standardizedFileURL.path)
        }
        if let scenes = scenesRootURL {
            expanded.insert(scenes.standardizedFileURL.path)
        }
        expandedFolders = expanded
    }
    
    private func buildFolderTree(_ url: URL) -> FolderNode {
        let children = (try? FileManager.default.contentsOfDirectory(
            at: url,
            includingPropertiesForKeys: [.isDirectoryKey],
            options: [.skipsHiddenFiles]
        )) ?? []
        
        let folders = children.filter { child in
            (try? child.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true
        }
        .sorted { $0.lastPathComponent.localizedCaseInsensitiveCompare($1.lastPathComponent) == .orderedAscending }
        
        let nodes = folders.map { buildFolderTree($0) }
        return FolderNode(url: url, name: url.lastPathComponent, children: nodes)
    }
    
    private func openImporter() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = true
        panel.prompt = "Import"
        let response = panel.runModal()
        guard response == .OK else { return }
        for url in panel.urls {
            handleImportURL(url)
        }
    }
    
    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        for provider in providers where provider.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) {
            provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                DispatchQueue.main.async {
                    if let data = item as? Data,
                       let url = URL(dataRepresentation: data, relativeTo: nil) {
                        handleImportURL(url)
                    } else if let url = item as? URL {
                        handleImportURL(url)
                    }
                }
            }
        }
        
        return true
    }
    
    private func isModelFile(_ url: URL) -> Bool {
        modelExtensions.contains(url.pathExtension.lowercased())
    }
    
    private func isSupportedAssetFile(_ url: URL) -> Bool {
        let ext = url.pathExtension.lowercased()
        return textureExtensions.contains(ext) || hdriExtensions.contains(ext)
    }
    
    private func isDirectory(_ url: URL) -> Bool {
        var isDir: ObjCBool = false
        FileManager.default.fileExists(atPath: url.path, isDirectory: &isDir)
        return isDir.boolValue
    }

    private func handleImportURL(_ url: URL) {
        if isDirectory(url) {
            importFolder(url)
            return
        }
        if isModelFile(url) {
            editorState.importModel(from: url)
            return
        }
        if isSupportedAssetFile(url) {
            importAssetFile(url)
            return
        }
        editorState.addLog(.warning, "Unsupported import: \(url.lastPathComponent)")
    }
    
    private func isUnderRoot(_ url: URL, root: URL) -> Bool {
        let rootPath = root.standardizedFileURL.path
        let path = url.standardizedFileURL.path
        return path == rootPath || path.hasPrefix(rootPath + "/")
    }
    
    private func importFolder(_ url: URL) {
        guard let assetsRoot = assetsRootURL else {
            editorState.addLog(.warning, "Select a project or asset root first.")
            return
        }
        let fileManager = FileManager.default
        var targetURL = assetsRoot.appendingPathComponent(url.lastPathComponent)
        var counter = 1
        while fileManager.fileExists(atPath: targetURL.path) {
            targetURL = assetsRoot.appendingPathComponent("\(url.lastPathComponent)_\(counter)")
            counter += 1
        }
        do {
            try fileManager.copyItem(at: url, to: targetURL)
            editorState.addLog(.info, "Imported folder: \(targetURL.lastPathComponent)")
            editorState.rescanAssetRoot()
        } catch {
            editorState.addLog(.error, "Folder import failed: \(error.localizedDescription)")
        }
    }
    
    private func importAssetFile(_ url: URL) {
        guard let assetsRoot = assetsRootURL else {
            editorState.addLog(.warning, "Select a project or asset root first.")
            return
        }
        let type: String
        if textureExtensions.contains(url.pathExtension.lowercased()) {
            type = "texture"
        } else if hdriExtensions.contains(url.pathExtension.lowercased()) {
            type = "hdri"
        } else {
            type = ""
        }
        let imported = CrescentEngineBridge.shared().importAsset(path: url.path, type: type)
        if !imported.isEmpty || isUnderRoot(url, root: assetsRoot) {
            editorState.rescanAssetRoot()
            editorState.addLog(.info, "Imported asset: \(url.lastPathComponent)")
        } else {
            editorState.addLog(.error, "Asset import failed: \(url.lastPathComponent)")
        }
    }
    
    private func revealInFinder(_ asset: AssetInfo) {
        let url = URL(fileURLWithPath: asset.path)
        NSWorkspace.shared.activateFileViewerSelecting([url])
    }

    private func openSceneAsset(_ asset: AssetInfo) {
        guard asset.type == .scene else { return }
        let url = URL(fileURLWithPath: asset.path)
        editorState.openScene(at: url)
    }
    
    private func promptRename(_ asset: AssetInfo) {
        let alert = NSAlert()
        alert.messageText = "Rename Asset"
        alert.informativeText = asset.name
        let input = NSTextField(string: asset.name)
        input.frame = NSRect(x: 0, y: 0, width: 240, height: 24)
        alert.accessoryView = input
        alert.addButton(withTitle: "Rename")
        alert.addButton(withTitle: "Cancel")
        
        let response = alert.runModal()
        guard response == .alertFirstButtonReturn else { return }
        
        var newName = input.stringValue.trimmingCharacters(in: CharacterSet.whitespacesAndNewlines)
        guard !newName.isEmpty else { return }
        
        let url = URL(fileURLWithPath: asset.path)
        let ext = url.pathExtension
        if URL(fileURLWithPath: newName).pathExtension.isEmpty, !ext.isEmpty {
            newName += ".\(ext)"
        }
        
        let newURL = url.deletingLastPathComponent().appendingPathComponent(newName)
        if FileManager.default.fileExists(atPath: newURL.path) {
            editorState.addLog(.warning, "Asset already exists: \(newName)")
            return
        }
        
        if let assetsRoot = assetsRootURL,
           isUnderRoot(url, root: assetsRoot),
           (isModelFile(url) || isSupportedAssetFile(url)) {
            let moved = CrescentEngineBridge.shared().moveAsset(source: url.path, to: newURL.path, overwrite: false)
            if moved {
                editorState.rescanAssetRoot()
            } else {
                editorState.addLog(.error, "Rename failed: \(newName)")
            }
            return
        }

        do {
            try FileManager.default.moveItem(at: url, to: newURL)
            editorState.rescanAssetRoot()
        } catch {
            editorState.addLog(.error, "Rename failed: \(error.localizedDescription)")
        }
    }

    private func loadSelectedAssetSettings() {
        guard let asset = selectedAsset else {
            selectedAssetGuid = ""
            selectedModelOptions = ModelImportOptions()
            selectedTextureOptions = TextureImportOptions()
            selectedHdriOptions = HdriImportOptions()
            return
        }
        let meta = CrescentEngineBridge.shared().getAssetMeta(path: asset.path) as? [String: Any] ?? [:]
        let guid = meta["guid"] as? String ?? ""
        selectedAssetGuid = guid
        if guid.isEmpty {
            selectedModelOptions = ModelImportOptions()
            selectedTextureOptions = TextureImportOptions()
            selectedHdriOptions = HdriImportOptions()
            return
        }
        if let model = meta["model"] as? [String: Any] {
            selectedModelOptions = ModelImportOptions(dictionary: model)
        }
        if let texture = meta["texture"] as? [String: Any] {
            selectedTextureOptions = TextureImportOptions(dictionary: texture)
        }
        if let hdri = meta["hdri"] as? [String: Any] {
            selectedHdriOptions = HdriImportOptions(dictionary: hdri)
        }
    }

    private func applySelectedAssetSettings() {
        guard let asset = selectedAsset, !selectedAssetGuid.isEmpty else { return }
        let bridge = CrescentEngineBridge.shared()
        switch asset.type {
        case .model:
            let ok = bridge.updateModelImportSettings(guid: selectedAssetGuid, settings: selectedModelOptions.toDictionary())
            editorState.addLog(ok ? .info : .error, ok ? "Model import settings saved." : "Failed to save model import settings.")
        case .texture:
            let ok = bridge.updateTextureImportSettings(guid: selectedAssetGuid, settings: selectedTextureOptions.toDictionary())
            editorState.addLog(ok ? .info : .error, ok ? "Texture import settings saved." : "Failed to save texture import settings.")
        case .hdri:
            let ok = bridge.updateHdriImportSettings(guid: selectedAssetGuid, settings: selectedHdriOptions.toDictionary())
            editorState.addLog(ok ? .info : .error, ok ? "HDRI import settings saved." : "Failed to save HDRI import settings.")
        default:
            break
        }
    }

    private func reimportSelectedAsset() {
        guard let asset = selectedAsset, !selectedAssetGuid.isEmpty else { return }
        applySelectedAssetSettings()
        let bridge = CrescentEngineBridge.shared()
        let ok: Bool
        switch asset.type {
        case .model:
            ok = bridge.reimportModelAsset(guid: selectedAssetGuid)
        case .texture:
            ok = bridge.reimportTextureAsset(guid: selectedAssetGuid)
        case .hdri:
            ok = bridge.reimportHdriAsset(guid: selectedAssetGuid)
        default:
            return
        }
        editorState.addLog(ok ? .info : .warning, ok ? "Reimported \(asset.name)" : "Reimport skipped: \(asset.name)")
    }

}

private struct FolderNode: Identifiable, Hashable {
    let url: URL
    let name: String
    let children: [FolderNode]
    
    var id: String { url.standardizedFileURL.path }
}

private struct AssetFolderSidebar: View {
    let folderTree: FolderNode?
    @Binding var selectedFolderURL: URL?
    @Binding var expandedFolders: Set<String>
    @ObservedObject var editorState: EditorState
    let onRescan: () -> Void
    let onSelectRoot: () -> Void
    @State private var showImportOptions: Bool = true
    
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Project")
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                Button("Rescan") {
                    onRescan()
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
            }
            
            if let tree = folderTree {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 4) {
                        FolderRow(
                            node: tree,
                            level: 0,
                            selectedFolderURL: $selectedFolderURL,
                            expandedFolders: $expandedFolders
                        )
                    }
                    .padding(.vertical, 4)
                }
                .frame(maxHeight: .infinity)
            } else {
                Button("Select Folder") {
                    onSelectRoot()
                }
                .buttonStyle(.bordered)
            }
            
            Spacer(minLength: 0)
            DisclosureGroup(isExpanded: $showImportOptions) {
                ImportOptionsPanel(editorState: editorState, showHeader: false)
                    .padding(.top, 6)
            } label: {
                HStack {
                    Text("Import Options")
                        .font(EditorTheme.fontBodyMedium)
                    Spacer()
                }
                .foregroundColor(EditorTheme.textPrimary)
            }
        }
        .padding(10)
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(10)
    }
    
}

private struct FolderRow: View {
    let node: FolderNode
    let level: Int
    @Binding var selectedFolderURL: URL?
    @Binding var expandedFolders: Set<String>
    
    var body: some View {
        let isExpanded = expandedFolders.contains(node.id)
        let isSelected = selectedFolderURL?.standardizedFileURL.path == node.id
        
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                if !node.children.isEmpty {
                    Button(action: {
                        if isExpanded {
                            expandedFolders.remove(node.id)
                        } else {
                            expandedFolders.insert(node.id)
                        }
                    }) {
                        Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                            .font(EditorTheme.fontCaption)
                            .foregroundColor(EditorTheme.textMuted)
                            .frame(width: 14)
                    }
                    .buttonStyle(.plain)
                } else {
                    Image(systemName: "chevron.right")
                        .font(EditorTheme.fontCaption)
                        .foregroundColor(Color.clear)
                        .frame(width: 14)
                }
                
                Image(systemName: "folder")
                    .font(EditorTheme.fontBody)
                    .foregroundColor(EditorTheme.textMuted)
                
                Text(node.name)
                    .font(EditorTheme.fontBody)
                    .foregroundColor(isSelected ? EditorTheme.textPrimary : EditorTheme.textMuted)
                    .lineLimit(1)
                
                Spacer()
            }
            .padding(.vertical, 4)
            .padding(.leading, CGFloat(level) * 12)
            .background(isSelected ? EditorTheme.textAccent.opacity(0.15) : Color.clear)
            .cornerRadius(6)
            .contentShape(Rectangle())
            .onTapGesture {
                selectedFolderURL = node.url
            }
            
            if isExpanded {
                ForEach(node.children) { child in
                    FolderRow(
                        node: child,
                        level: level + 1,
                        selectedFolderURL: $selectedFolderURL,
                        expandedFolders: $expandedFolders
                    )
                }
            }
        }
    }
}

private struct AssetGridPanel: View {
    let assets: [AssetInfo]
    @Binding var gridSize: CGFloat
    @Binding var viewMode: AssetViewMode
    @Binding var selectedFilter: AssetFilter
    @Binding var searchText: String
    @Binding var selectedAssetID: UUID?
    @Binding var isDropping: Bool
    let assetRootURL: URL?
    let onDrop: ([NSItemProvider]) -> Bool
    let onOpenScene: (AssetInfo) -> Void
    let onReveal: (AssetInfo) -> Void
    let onRename: (AssetInfo) -> Void
    @State private var hoveredAsset: AssetInfo?
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 12) {
                FilterTabs(selected: $selectedFilter)
                    .frame(maxWidth: .infinity, alignment: .leading)
                
                HStack(spacing: 6) {
                    Image(systemName: "magnifyingglass")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(EditorTheme.textMuted)
                    TextField("Search", text: $searchText)
                        .textFieldStyle(.plain)
                        .font(EditorTheme.fontBody)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(EditorTheme.surface)
                .cornerRadius(8)
                .frame(width: 200)
                
                Picker("", selection: $viewMode) {
                    Image(systemName: "square.grid.2x2").tag(AssetViewMode.grid)
                    Image(systemName: "list.bullet").tag(AssetViewMode.list)
                }
                .labelsHidden()
                .pickerStyle(.segmented)
                .frame(width: 90)
                
                if viewMode == .grid {
                    HStack(spacing: 6) {
                        Image(systemName: "rectangle.grid.2x2")
                            .font(EditorTheme.fontCaption)
                        Slider(value: $gridSize, in: 120...220, step: 10)
                            .frame(width: 120)
                    }
                    .foregroundColor(EditorTheme.textMuted)
                }
            }
            
            ZStack {
                ScrollView {
                    if assets.isEmpty {
                        EmptyAssetState(searchText: searchText, assetRootURL: assetRootURL)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 20)
                    } else {
                        if viewMode == .grid {
                            LazyVGrid(columns: [GridItem(.adaptive(minimum: gridSize), spacing: 16)], spacing: 16) {
                                ForEach(assets) { asset in
                                    AssetCard(
                                        asset: asset,
                                        size: gridSize,
                                        isSelected: selectedAssetID == asset.id,
                                        onSelect: {
                                            selectedAssetID = asset.id
                                        },
                                        onOpen: {
                                            if asset.type == .scene {
                                                onOpenScene(asset)
                                            }
                                        },
                                        onReveal: { onReveal(asset) },
                                        onRename: { onRename(asset) },
                                        onHover: { hovered in
                                            if let hovered {
                                                hoveredAsset = hovered
                                            } else if hoveredAsset?.id == asset.id {
                                                hoveredAsset = nil
                                            }
                                        }
                                    )
                                }
                            }
                            .padding(.vertical, 4)
                        } else {
                            LazyVStack(alignment: .leading, spacing: 6) {
                                ForEach(assets) { asset in
                                    AssetListRow(
                                        asset: asset,
                                        isSelected: selectedAssetID == asset.id,
                                        onSelect: {
                                            selectedAssetID = asset.id
                                        },
                                        onOpen: {
                                            if asset.type == .scene {
                                                onOpenScene(asset)
                                            }
                                        },
                                        onReveal: { onReveal(asset) },
                                        onRename: { onRename(asset) },
                                        onHover: { hovered in
                                            if let hovered {
                                                hoveredAsset = hovered
                                            } else if hoveredAsset?.id == asset.id {
                                                hoveredAsset = nil
                                            }
                                        }
                                    )
                                }
                            }
                            .padding(.vertical, 4)
                        }
                    }
                }
                .scrollIndicators(.visible)
                .frame(maxHeight: .infinity)
                
                if isDropping {
                    RoundedRectangle(cornerRadius: 12)
                        .fill(EditorTheme.textAccent.opacity(0.12))
                        .overlay(
                            RoundedRectangle(cornerRadius: 12)
                                .stroke(EditorTheme.textAccent, style: StrokeStyle(lineWidth: 1, dash: [6]))
                        )
                        .overlay(
                            Text("Drop assets to import")
                                .font(EditorTheme.fontBodyMedium)
                                .foregroundColor(EditorTheme.textPrimary)
                        )
                        .padding(12)
                }
                
                if let hoveredAsset {
                    AssetHoverPreview(asset: hoveredAsset)
                        .frame(width: 240)
                        .padding(12)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                        .allowsHitTesting(false)
                        .transition(.opacity)
                }
            }
            .frame(maxHeight: .infinity, alignment: .top)
            .onDrop(of: [UTType.fileURL], isTargeted: $isDropping, perform: onDrop)
        }
        .padding(10)
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(10)
    }
}

private enum AssetViewMode: String {
    case grid
    case list
}

private enum AssetFilter: String, CaseIterable, Identifiable {
    case all
    case model
    case texture
    case hdri
    case material
    case scene
    
    var id: String { rawValue }
    
    var title: String {
        switch self {
        case .all:
            return "All"
        case .model:
            return "Models"
        case .texture:
            return "Textures"
        case .hdri:
            return "HDRIs"
        case .material:
            return "Materials"
        case .scene:
            return "Scenes"
        }
    }
    
    var assetType: AssetInfo.AssetType {
        switch self {
        case .all:
            return .model
        case .model:
            return .model
        case .texture:
            return .texture
        case .hdri:
            return .hdri
        case .material:
            return .material
        case .scene:
            return .scene
        }
    }
}

private struct FilterTabs: View {
    @Binding var selected: AssetFilter
    
    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(AssetFilter.allCases) { filter in
                    Button(action: {
                        selected = filter
                    }) {
                        Text(filter.title)
                            .font(EditorTheme.fontBody)
                            .foregroundColor(selected == filter ? EditorTheme.textPrimary : EditorTheme.textMuted)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 6)
                            .background(selected == filter ? EditorTheme.surfaceElevated : EditorTheme.surface)
                            .overlay(
                                RoundedRectangle(cornerRadius: 8)
                                    .stroke(selected == filter ? EditorTheme.textAccent.opacity(0.6) : EditorTheme.panelStroke, lineWidth: 1)
                            )
                            .cornerRadius(8)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
    }
}

private struct ImportOptionsPanel: View {
    @ObservedObject var editorState: EditorState
    let showHeader: Bool
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if showHeader {
                Text("Import Options")
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
            }
            
            HStack(spacing: 8) {
                Text("Scale")
                    .font(EditorTheme.fontBody)
                    .foregroundColor(EditorTheme.textMuted)
                TextField("", value: scaleBinding, format: .number.precision(.fractionLength(3)))
                    .textFieldStyle(.plain)
                    .font(EditorTheme.fontMono)
                    .multilineTextAlignment(.trailing)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 4)
                    .background(EditorTheme.panelBackground)
                    .cornerRadius(4)
                    .frame(width: 80)
                
                Spacer()
                
                Button("Reset") {
                    editorState.modelImportOptions = ModelImportOptions()
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
            }
            
            Toggle("Flip UVs", isOn: flipUVBinding)
                .font(EditorTheme.fontBody)
            
            Toggle("Import LOD0 Only", isOn: lodBinding)
                .font(EditorTheme.fontBody)

            Toggle("Merge Static Meshes", isOn: mergeStaticBinding)
                .font(EditorTheme.fontBody)
        }
        .padding(10)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(10)
    }
    
    private var scaleBinding: Binding<Double> {
        Binding(
            get: { editorState.modelImportOptions.scale },
            set: { newValue in
                editorState.modelImportOptions.scale = max(0.0001, newValue)
            }
        )
    }
    
    private var flipUVBinding: Binding<Bool> {
        Binding(
            get: { editorState.modelImportOptions.flipUVs },
            set: { editorState.modelImportOptions.flipUVs = $0 }
        )
    }
    
    private var lodBinding: Binding<Bool> {
        Binding(
            get: { editorState.modelImportOptions.onlyLOD0 },
            set: { editorState.modelImportOptions.onlyLOD0 = $0 }
        )
    }

    private var mergeStaticBinding: Binding<Bool> {
        Binding(
            get: { editorState.modelImportOptions.mergeStaticMeshes },
            set: { editorState.modelImportOptions.mergeStaticMeshes = $0 }
        )
    }
}

private struct AssetImportSettingsPanel: View {
    let asset: AssetInfo
    @Binding var modelOptions: ModelImportOptions
    @Binding var textureOptions: TextureImportOptions
    @Binding var hdriOptions: HdriImportOptions
    let onApply: () -> Void
    let onReimport: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Asset Settings")
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                Text(asset.name)
                    .font(EditorTheme.fontCaption)
                    .foregroundColor(EditorTheme.textMuted)
                    .lineLimit(1)
            }

            Divider()
                .overlay(EditorTheme.panelStroke)

            switch asset.type {
            case .model:
                modelSettings
            case .texture:
                textureSettings
            case .hdri:
                hdriSettings
            default:
                Text("No import settings for this asset type.")
                    .font(EditorTheme.fontBody)
                    .foregroundColor(EditorTheme.textMuted)
            }

            HStack(spacing: 8) {
                Button("Apply") { onApply() }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                Button("Reimport") { onReimport() }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.small)
                Spacer()
            }
        }
        .padding(10)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(10)
    }

    private var modelSettings: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Text("Scale")
                    .font(EditorTheme.fontBody)
                    .foregroundColor(EditorTheme.textMuted)
                TextField("", value: Binding(
                    get: { modelOptions.scale },
                    set: { modelOptions.scale = max(0.0001, $0) }
                ), format: .number.precision(.fractionLength(3)))
                    .textFieldStyle(.plain)
                    .font(EditorTheme.fontMono)
                    .multilineTextAlignment(.trailing)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 4)
                    .background(EditorTheme.panelBackground)
                    .cornerRadius(4)
                    .frame(width: 80)
                Spacer()
            }

            Toggle("Flip UVs", isOn: Binding(
                get: { modelOptions.flipUVs },
                set: { modelOptions.flipUVs = $0 }
            ))
            .font(EditorTheme.fontBody)

            Toggle("Import LOD0 Only", isOn: Binding(
                get: { modelOptions.onlyLOD0 },
                set: { modelOptions.onlyLOD0 = $0 }
            ))
            .font(EditorTheme.fontBody)

            Toggle("Merge Static Meshes", isOn: Binding(
                get: { modelOptions.mergeStaticMeshes },
                set: { modelOptions.mergeStaticMeshes = $0 }
            ))
            .font(EditorTheme.fontBody)
        }
    }

    private var textureSettings: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("sRGB", isOn: Binding(
                get: { textureOptions.srgb },
                set: { textureOptions.srgb = $0 }
            ))
            .font(EditorTheme.fontBody)

            Toggle("Flip Y", isOn: Binding(
                get: { textureOptions.flipY },
                set: { textureOptions.flipY = $0 }
            ))
            .font(EditorTheme.fontBody)

            Toggle("Normal Map", isOn: Binding(
                get: { textureOptions.normalMap },
                set: { textureOptions.normalMap = $0 }
            ))
            .font(EditorTheme.fontBody)
        }
    }

    private var hdriSettings: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("Flip Y", isOn: Binding(
                get: { hdriOptions.flipY },
                set: { hdriOptions.flipY = $0 }
            ))
            .font(EditorTheme.fontBody)
        }
    }
}

private struct AssetCard: View {
    let asset: AssetInfo
    let size: CGFloat
    let isSelected: Bool
    let onSelect: () -> Void
    let onOpen: () -> Void
    let onReveal: () -> Void
    let onRename: () -> Void
    let onHover: (AssetInfo?) -> Void
    @State private var isHovering: Bool = false
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            AssetThumbnail(asset: asset)
                .frame(height: size * 0.6)
                .clipShape(RoundedRectangle(cornerRadius: 8))
            
            Text(asset.name)
                .font(EditorTheme.fontBodyMedium)
                .foregroundColor(EditorTheme.textPrimary)
                .lineLimit(1)
            
            Text(asset.type.rawValue.uppercased())
                .font(EditorTheme.fontCaption)
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(10)
        .frame(width: size)
        .background(isSelected ? EditorTheme.textAccent.opacity(0.15) : EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(isSelected ? EditorTheme.textAccent : EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(12)
        .contentShape(Rectangle())
        .onTapGesture {
            onSelect()
        }
        .onTapGesture(count: 2) {
            onOpen()
        }
        .onHover { hovering in
            isHovering = hovering
            onHover(hovering ? asset : nil)
        }
        .contextMenu {
            Button("Reveal in Finder") { onReveal() }
            Button("Rename") { onRename() }
        }
        .overlay(alignment: .topTrailing) {
            if isHovering {
                Image(systemName: "eye")
                    .font(EditorTheme.fontCaption)
                    .foregroundColor(EditorTheme.textMuted)
                    .padding(6)
                    .background(EditorTheme.surface)
                    .cornerRadius(6)
                    .padding(6)
            }
        }
        .onDrag {
            NSItemProvider(object: URL(fileURLWithPath: asset.path) as NSURL)
        }
    }
}

private struct AssetListRow: View {
    let asset: AssetInfo
    let isSelected: Bool
    let onSelect: () -> Void
    let onOpen: () -> Void
    let onReveal: () -> Void
    let onRename: () -> Void
    let onHover: (AssetInfo?) -> Void
    @State private var isHovering: Bool = false
    
    var body: some View {
        HStack(spacing: 10) {
            AssetThumbnail(asset: asset)
                .frame(width: 36, height: 36)
                .clipShape(RoundedRectangle(cornerRadius: 8))
            
            VStack(alignment: .leading, spacing: 2) {
                Text(asset.name)
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
                    .lineLimit(1)
                Text(asset.relativePath)
                    .font(EditorTheme.fontCaption)
                    .foregroundColor(EditorTheme.textMuted)
                    .lineLimit(1)
            }
            
            Spacer()
            
            Text(asset.type.rawValue.uppercased())
                .font(EditorTheme.fontCaption)
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(isSelected ? EditorTheme.textAccent.opacity(0.15) : EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(isSelected ? EditorTheme.textAccent : EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(10)
        .contentShape(Rectangle())
        .onTapGesture {
            onSelect()
        }
        .onTapGesture(count: 2) {
            onOpen()
        }
        .onHover { hovering in
            isHovering = hovering
            onHover(hovering ? asset : nil)
        }
        .contextMenu {
            Button("Reveal in Finder") { onReveal() }
            Button("Rename") { onRename() }
        }
        .overlay(alignment: .trailing) {
            if isHovering {
                Image(systemName: "eye")
                    .font(EditorTheme.fontCaption)
                    .foregroundColor(EditorTheme.textMuted)
                    .padding(6)
                    .background(EditorTheme.surface)
                    .cornerRadius(6)
                    .padding(.trailing, 6)
            }
        }
        .onDrag {
            NSItemProvider(object: URL(fileURLWithPath: asset.path) as NSURL)
        }
    }
}

private struct AssetHoverPreview: View {
    let asset: AssetInfo
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            AssetThumbnail(asset: asset)
                .frame(height: 140)
                .clipShape(RoundedRectangle(cornerRadius: 10))
            
            Text(asset.name)
                .font(EditorTheme.fontBodyMedium)
                .foregroundColor(EditorTheme.textPrimary)
                .lineLimit(1)
            
            Text(asset.relativePath)
                .font(EditorTheme.fontCaption)
                .foregroundColor(EditorTheme.textMuted)
                .lineLimit(2)
            
            Text(asset.type.rawValue.uppercased())
                .font(EditorTheme.fontCaption)
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(10)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(12)
    }
}

private struct AssetThumbnail: View {
    let asset: AssetInfo
    @State private var previewImage: NSImage?
    
    var body: some View {
        ZStack {
            if let previewImage {
                Image(nsImage: previewImage)
                    .resizable()
                    .scaledToFill()
            } else {
                RoundedRectangle(cornerRadius: 8)
                    .fill(EditorTheme.surfaceElevated)
                    .overlay(
                        Image(systemName: iconForAsset(asset.type))
                            .font(EditorTheme.font(size: 20, weight: .semibold))
                            .foregroundColor(EditorTheme.textMuted)
                    )
            }
        }
        .onAppear {
            loadPreviewIfNeeded()
        }
    }
    
    private func loadPreviewIfNeeded() {
        guard previewImage == nil else { return }
        switch asset.type {
        case .texture, .hdri:
            previewImage = NSImage(contentsOfFile: asset.path)
        default:
            break
        }
    }
    
    private func iconForAsset(_ type: AssetInfo.AssetType) -> String {
        switch type {
        case .model:
            return "cube.fill"
        case .texture:
            return "photo.fill"
        case .hdri:
            return "sparkles"
        case .material:
            return "paintpalette.fill"
        case .scene:
            return "film"
        }
    }
}

private struct EmptyAssetState: View {
    let searchText: String
    let assetRootURL: URL?
    
    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: "tray")
                .font(EditorTheme.fontHero)
                .foregroundColor(EditorTheme.textMuted)
            
            Text(emptyTitle)
                .font(EditorTheme.fontBodyMedium)
                .foregroundColor(EditorTheme.textPrimary)
            
            if !emptySubtitle.isEmpty {
                Text(emptySubtitle)
                    .font(EditorTheme.fontCaption)
                    .foregroundColor(EditorTheme.textMuted)
                    .multilineTextAlignment(.center)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 16)
        .background(EditorTheme.surface)
        .cornerRadius(10)
    }
    
    private var emptyTitle: String {
        if !searchText.isEmpty {
            return "No matches"
        }
        if assetRootURL == nil {
            return "No assets"
        }
        return "No assets found"
    }
    
    private var emptySubtitle: String {
        if !searchText.isEmpty {
            return "Try a different search term."
        }
        return ""
    }
}
