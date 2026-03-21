import SwiftUI
import Combine
import AppKit

// Entity info with UUID
struct EntityInfo: Identifiable, Hashable {
    let uuid: String
    let name: String
    let parentUUID: String
    let hasSkinned: Bool
    let hasAnimator: Bool
    let clipCount: Int
    
    var id: String { uuid }  // Use UUID as ID!
}

struct AssetInfo: Identifiable, Hashable {
    enum AssetType: String {
        case model
        case texture
        case hdri
        case audio
        case material
        case scene
    }
    
    let id = UUID()
    let name: String
    let path: String
    let relativePath: String
    let type: AssetType
    let modified: Date?
    let sizeBytes: Int64
}

struct ModelImportOptions: Hashable {
    var scale: Double = 1.0
    var flipUVs: Bool = false
    var onlyLOD0: Bool = false
    var mergeStaticMeshes: Bool = false
    
    func toDictionary() -> [String: Any] {
        [
            "scale": scale,
            "flipUVs": flipUVs,
            "onlyLOD0": onlyLOD0,
            "mergeStaticMeshes": mergeStaticMeshes
        ]
    }

    init() {}

    init(dictionary: [String: Any]) {
        scale = (dictionary["scale"] as? NSNumber)?.doubleValue ?? 1.0
        flipUVs = (dictionary["flipUVs"] as? NSNumber)?.boolValue ?? false
        onlyLOD0 = (dictionary["onlyLOD0"] as? NSNumber)?.boolValue ?? false
        mergeStaticMeshes = (dictionary["mergeStaticMeshes"] as? NSNumber)?.boolValue ?? false
    }
}

struct TextureImportOptions: Hashable {
    var srgb: Bool = true
    var generateMipmaps: Bool = true
    var flipY: Bool = false
    var maxSize: Int = 4096
    var normalMap: Bool = false

    func toDictionary() -> [String: Any] {
        [
            "srgb": srgb,
            "generateMipmaps": generateMipmaps,
            "flipY": flipY,
            "maxSize": maxSize,
            "normalMap": normalMap
        ]
    }

    init() {}

    init(dictionary: [String: Any]) {
        srgb = (dictionary["srgb"] as? NSNumber)?.boolValue ?? true
        generateMipmaps = (dictionary["generateMipmaps"] as? NSNumber)?.boolValue ?? true
        flipY = (dictionary["flipY"] as? NSNumber)?.boolValue ?? false
        maxSize = (dictionary["maxSize"] as? NSNumber)?.intValue ?? 4096
        normalMap = (dictionary["normalMap"] as? NSNumber)?.boolValue ?? false
    }
}

struct HdriImportOptions: Hashable {
    var flipY: Bool = false
    var maxSize: Int = 2048

    func toDictionary() -> [String: Any] {
        [
            "flipY": flipY,
            "maxSize": maxSize
        ]
    }

    init() {}

    init(dictionary: [String: Any]) {
        flipY = (dictionary["flipY"] as? NSNumber)?.boolValue ?? false
        maxSize = (dictionary["maxSize"] as? NSNumber)?.intValue ?? 2048
    }
}

enum DockTab: String, CaseIterable, Identifiable {
    case assets
    case console
    
    var id: String { rawValue }
    
    var title: String {
        switch self {
        case .assets:
            return "Explorer"
        case .console:
            return "Console"
        }
    }
    
    var icon: String {
        switch self {
        case .assets:
            return "folder.fill"
        case .console:
            return "terminal.fill"
        }
    }
}

// Console Log Model
struct ConsoleLog: Identifiable {
    let id = UUID()
    let timestamp: Date
    let type: LogType
    let message: String
    
    enum LogType {
        case info, warning, error
        
        var icon: String {
            switch self {
            case .info: return "info.circle.fill"
            case .warning: return "exclamationmark.triangle.fill"
            case .error: return "xmark.circle.fill"
            }
        }
        
        var color: Color {
            switch self {
            case .info: return .blue
            case .warning: return .orange
            case .error: return .red
            }
        }
    }
}

enum ViewMode: Int, CaseIterable {
    case scene = 0
    case game = 1

    var label: String {
        switch self {
        case .scene: return "Scene"
        case .game: return "Game"
        }
    }

    var systemImage: String {
        switch self {
        case .scene: return "cube.transparent"
        case .game: return "play.tv"
        }
    }
}

// Editor state - Observable object to manage editor state
class EditorState: ObservableObject {
    private static let repositoryRootURL: URL = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    @Published var selectedEntityUUIDs: Set<String> = []  // Swift manages multi-selection
    @Published var primarySelectionUUID: String? = nil
    @Published var entityList: [EntityInfo] = []
    @Published var assets: [AssetInfo] = []
    @Published var assetRootURL: URL?
    @Published var assetRootName: String = "Select Folder"
    @Published var consoleLogs: [ConsoleLog] = []
    @Published var showInspector: Bool = true
    @Published var showHierarchy: Bool = true
    @Published var showAssets: Bool = true
    @Published var showConsole: Bool = true
    @Published var hierarchyCollapsed: Bool = false
    @Published var inspectorCollapsed: Bool = false
    @Published var dockCollapsed: Bool = false
    @Published var dockTab: DockTab = .assets
    @Published var modelImportOptions = ModelImportOptions()
    @Published var autoReimportAssets: Bool = true
    @Published var isPlaying: Bool = false
    @Published var isPaused: Bool = false
    @Published var timeScale: Float = 1.0
    @Published var viewMode: ViewMode = .scene
    @Published var sceneURL: URL?
    @Published var sceneName: String = "Untitled"
    @Published var projectName: String = ""
    @Published var projectRootURL: URL?
    @Published var projectAssetsURL: URL?
    @Published var projectScenesURL: URL?
    @Published var hasProject: Bool = false
    @Published var settingsWindowRequested: Bool = false
    @Published var isBuildingGame: Bool = false
    @Published var lastBuiltAppURL: URL?
    @Published var terrainPaintEnabled: Bool = false
    @Published var terrainBrushMode: TerrainBrushMode = .paint
    @Published var terrainPaintLayer: Int = 0
    @Published var terrainSculptTool: TerrainSculptTool = .raise
    @Published var terrainSculptResolution: Int = 128
    @Published var terrainBrushRadius: Float = 1.5
    @Published var terrainBrushHardness: Float = 0.65
    @Published var terrainBrushStrength: Float = 0.35
    @Published var terrainBrushSpacing: Float = 0.25
    @Published var terrainBrushMaskPreset: Int = 0
    @Published var terrainBrushMaskPath: String = ""
    @Published var terrainBrushAutoNormalize: Bool = true
    
    private var entityRefreshTimer: Timer?
    private var selectionRefreshTimer: Timer?
    private var lastEngineSelectionUUIDs: Set<String> = []  // Track engine selection changes
    private var assetRootAccessActive: Bool = false
    private var projectRootAccessActive: Bool = false
    private var projectRootAccessURL: URL?
    private var hotReloadCancellable: AnyCancellable?
    private var assetModDates: [String: Date] = [:]
    private var keyDownMonitor: Any?
    private var keyUpMonitor: Any?
    private var flagsChangedMonitor: Any?
    
    private static func logTypeForBuildLine(_ line: String) -> ConsoleLog.LogType {
        let lower = line.lowercased()
        if lower.contains("error") || lower.contains("failed") || lower.contains("traceback") {
            return .error
        }
        if lower.contains("warning") {
            return .warning
        }
        return .info
    }

    var terrainPaintConfig: TerrainPaintConfig {
        TerrainPaintConfig(
            enabled: terrainPaintEnabled && !isPlaying && viewMode == .scene,
            targetEntityUUID: primarySelectionUUID ?? "",
            mode: terrainBrushMode,
            layer: terrainPaintLayer,
            sculptTool: terrainSculptTool,
            sculptResolution: terrainSculptResolution,
            radius: terrainBrushRadius,
            hardness: terrainBrushHardness,
            strength: terrainBrushStrength,
            spacing: terrainBrushSpacing,
            maskPreset: terrainBrushMaskPreset,
            maskPath: terrainBrushMaskPath,
            autoNormalize: terrainBrushAutoNormalize
        )
    }
    
    init() {
        addLog(.info, "Crescent Engine Editor Started")
        addLog(.info, "Initializing Metal Renderer...")
        
        // Keep selection responsive, but refresh the full hierarchy less aggressively.
        selectionRefreshTimer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
            self?.refreshSelectionState()
        }
        entityRefreshTimer = Timer.scheduledTimer(withTimeInterval: 1.25, repeats: true) { [weak self] _ in
            self?.refreshEntityList()
        }

        installKeyboardMonitors()
        startHotReloadTimer()
    }
    
    deinit {
        entityRefreshTimer?.invalidate()
        selectionRefreshTimer?.invalidate()
        hotReloadCancellable?.cancel()
        removeKeyboardMonitors()
        if assetRootAccessActive, let url = assetRootURL {
            url.stopAccessingSecurityScopedResource()
        }
        if projectRootAccessActive, let url = projectRootAccessURL {
            url.stopAccessingSecurityScopedResource()
        }
    }
    
    func refreshEntityList() {
        let bridge = CrescentEngineBridge.shared()
        let entityInfos = bridge.getAllEntityInfo() as! [[String: Any]]
        
        let newEntityList = entityInfos.map { info in
            let uuid = info["uuid"] as? String ?? ""
            let name = info["name"] as? String ?? "Entity"
            let parentUUID = info["parent"] as? String ?? ""
            let hasSkinned = (info["skinned"] as? NSNumber)?.boolValue ?? false
            let hasAnimator = (info["animator"] as? NSNumber)?.boolValue ?? false
            let clipCount = (info["clipCount"] as? NSNumber)?.intValue ?? 0
            return EntityInfo(uuid: uuid, name: name, parentUUID: parentUUID, hasSkinned: hasSkinned, hasAnimator: hasAnimator, clipCount: clipCount)
        }
        
        // Only update if list actually changed
        if newEntityList != entityList {
            entityList = newEntityList
            
            // Remove deleted entities from selection
            let validUUIDs = Set(entityList.map { $0.uuid })
            selectedEntityUUIDs = selectedEntityUUIDs.filter { validUUIDs.contains($0) }
            if let primary = primarySelectionUUID, !selectedEntityUUIDs.contains(primary) {
                primarySelectionUUID = pickPrimarySelection(from: selectedEntityUUIDs)
            }
        }
        
        refreshSelectionState()
    }

    func refreshSelectionState() {
        let bridge = CrescentEngineBridge.shared()
        let engineSelectionOrder = bridge.getAllSelectedUUIDs()
        let engineSelectedUUIDs = Set(engineSelectionOrder)
        let enginePrimary = engineSelectionOrder.first
        
        // If engine selection changed, sync to Swift
        if engineSelectedUUIDs != lastEngineSelectionUUIDs || enginePrimary != primarySelectionUUID {
            lastEngineSelectionUUIDs = engineSelectedUUIDs
            selectedEntityUUIDs = engineSelectedUUIDs
            primarySelectionUUID = enginePrimary
        }
        // If engine selection didn't change, keep Swift selection as-is
    }
    
    func selectEntity(_ uuid: String, toggle: Bool = false) {
        if toggle {
            if selectedEntityUUIDs.contains(uuid) {
                selectedEntityUUIDs.remove(uuid)
                if primarySelectionUUID == uuid {
                    primarySelectionUUID = pickPrimarySelection(from: selectedEntityUUIDs)
                }
            } else {
                selectedEntityUUIDs.insert(uuid)
                primarySelectionUUID = uuid
            }
        } else {
            selectedEntityUUIDs = [uuid]
            primarySelectionUUID = uuid
        }
        
        syncSelectionToEngine()
    }
    
    func deleteSelected() {
        guard !selectedEntityUUIDs.isEmpty else { return }
        
        // Get names for logging
        let selectedEntities = entityList.filter { selectedEntityUUIDs.contains($0.uuid) }
        let names = selectedEntities.map { $0.name }
        
        // Delete by UUID (not by name!)
        let uuids = Array(selectedEntityUUIDs)
        CrescentEngineBridge.shared().deleteEntities(uuids: uuids)
        
        let joined = names.joined(separator: ", ")
        addLog(.warning, "Deleted: \(joined)")
        selectedEntityUUIDs = []
        primarySelectionUUID = nil
        refreshEntityList()
    }

    func duplicateSelected() {
        guard !selectedEntityUUIDs.isEmpty else { return }

        let uuids = Array(selectedEntityUUIDs)
        let duplicated = CrescentEngineBridge.shared().duplicateEntities(uuids: uuids)
        refreshEntityList()
        if !duplicated.isEmpty {
            selectedEntityUUIDs = Set(duplicated)
            primarySelectionUUID = duplicated.first
            syncSelectionToEngine()
            addLog(.info, "Duplicated \(duplicated.count) object(s)")
        }
    }
    
    private func syncSelectionToEngine() {
        let uuids = orderedSelectionUUIDs()
        
        if uuids.isEmpty {
            CrescentEngineBridge.shared().clearSelection()
            lastEngineSelectionUUIDs = []  // Update tracker
            primarySelectionUUID = nil
        } else {
            if primarySelectionUUID == nil || !selectedEntityUUIDs.contains(primarySelectionUUID ?? "") {
                primarySelectionUUID = uuids.first
            }
            CrescentEngineBridge.shared().setSelection(uuids: uuids)
            lastEngineSelectionUUIDs = selectedEntityUUIDs  // Update tracker
            
            // Log selection
            if let firstUUID = uuids.first,
               let entity = entityList.first(where: { $0.uuid == firstUUID }) {
                let count = uuids.count
                if count > 1 {
                    addLog(.info, "Selected: \(entity.name) (+\(count - 1) more)")
                } else {
                    addLog(.info, "Selected: \(entity.name)")
                }
            }
        }
    }

    private func orderedSelectionUUIDs() -> [String] {
        guard !selectedEntityUUIDs.isEmpty else { return [] }
        var ordered: [String] = []
        if let primary = primarySelectionUUID, selectedEntityUUIDs.contains(primary) {
            ordered.append(primary)
        }
        for entity in entityList where selectedEntityUUIDs.contains(entity.uuid) {
            if entity.uuid != primarySelectionUUID {
                ordered.append(entity.uuid)
            }
        }
        if ordered.count < selectedEntityUUIDs.count {
            let missing = selectedEntityUUIDs.subtracting(Set(ordered)).sorted()
            ordered.append(contentsOf: missing)
        }
        return ordered
    }

    private func pickPrimarySelection(from selection: Set<String>) -> String? {
        for entity in entityList where selection.contains(entity.uuid) {
            return entity.uuid
        }
        return selection.first
    }
    
    // Console methods
    func addLog(_ type: ConsoleLog.LogType, _ message: String) {
        DispatchQueue.main.async {
            self.consoleLogs.append(ConsoleLog(timestamp: Date(), type: type, message: message))
            
            // Limit to last 1000 logs
            if self.consoleLogs.count > 1000 {
                self.consoleLogs.removeFirst(self.consoleLogs.count - 1000)
            }
        }
    }
    
    func clearConsole() {
        consoleLogs.removeAll()
        addLog(.info, "Console cleared")
    }

    private func installKeyboardMonitors() {
        if keyDownMonitor == nil {
            keyDownMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown]) { event in
                guard self.shouldCaptureKeyboardEvent() else { return event }
                CrescentEngineBridge.shared().handleKeyDown(event.keyCode)
                return nil
            }
        }
        if keyUpMonitor == nil {
            keyUpMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyUp]) { event in
                guard self.shouldCaptureKeyboardEvent() else { return event }
                CrescentEngineBridge.shared().handleKeyUp(event.keyCode)
                return nil
            }
        }
        if flagsChangedMonitor == nil {
            flagsChangedMonitor = NSEvent.addLocalMonitorForEvents(matching: [.flagsChanged]) { event in
                guard self.shouldCaptureKeyboardEvent() else { return event }
                self.handleModifierEvent(event)
                return event
            }
        }
    }

    private func removeKeyboardMonitors() {
        if let monitor = keyDownMonitor {
            NSEvent.removeMonitor(monitor)
            keyDownMonitor = nil
        }
        if let monitor = keyUpMonitor {
            NSEvent.removeMonitor(monitor)
            keyUpMonitor = nil
        }
        if let monitor = flagsChangedMonitor {
            NSEvent.removeMonitor(monitor)
            flagsChangedMonitor = nil
        }
    }

    private func shouldCaptureKeyboardEvent() -> Bool {
        guard let window = (NSApp.keyWindow ?? NSApp.mainWindow) else {
            return true
        }
        if window.firstResponder is NSTextView {
            return false
        }
        return true
    }

    private func handleModifierEvent(_ event: NSEvent) {
        switch event.keyCode {
        case 56, 60:
            if event.modifierFlags.contains(.shift) {
                CrescentEngineBridge.shared().handleKeyDown(event.keyCode)
            } else {
                CrescentEngineBridge.shared().handleKeyUp(event.keyCode)
            }
        case 59, 62:
            if event.modifierFlags.contains(.control) {
                CrescentEngineBridge.shared().handleKeyDown(event.keyCode)
            } else {
                CrescentEngineBridge.shared().handleKeyUp(event.keyCode)
            }
        case 58, 61:
            if event.modifierFlags.contains(.option) {
                CrescentEngineBridge.shared().handleKeyDown(event.keyCode)
            } else {
                CrescentEngineBridge.shared().handleKeyUp(event.keyCode)
            }
        case 55, 54:
            if event.modifierFlags.contains(.command) {
                CrescentEngineBridge.shared().handleKeyDown(event.keyCode)
            } else {
                CrescentEngineBridge.shared().handleKeyUp(event.keyCode)
            }
        default:
            break
        }
    }
    
    func setAssetRoot(from url: URL) {
        var resolvedURL = url
        let accessed = resolvedURL.startAccessingSecurityScopedResource()
        if accessed {
            resolvedURL = resolvedURL.standardizedFileURL
        }
        if assetRootAccessActive, let oldURL = assetRootURL {
            oldURL.stopAccessingSecurityScopedResource()
        }
        assetRootAccessActive = accessed
        assetRootURL = resolvedURL
        assetRootName = resolvedURL.lastPathComponent
        CrescentEngineBridge.shared().setAssetRoot(path: resolvedURL.path)
        rescanAssetRoot()
    }

    func refreshProjectInfo() {
        let bridge = CrescentEngineBridge.shared()
        guard let info = bridge.getProjectInfo() as? [String: Any], !info.isEmpty else {
            hasProject = false
            projectName = ""
            projectRootURL = nil
            projectAssetsURL = nil
            projectScenesURL = nil
            return
        }
        hasProject = true
        projectName = info["name"] as? String ?? projectName
        if let root = info["rootPath"] as? String {
            projectRootURL = URL(fileURLWithPath: root)
        }
        if let assets = info["assetsPath"] as? String {
            projectAssetsURL = URL(fileURLWithPath: assets)
            assetRootURL = projectAssetsURL
            assetRootName = projectName.isEmpty ? "Assets" : "\(projectName) Assets"
            CrescentEngineBridge.shared().setAssetRoot(path: assets)
        }
        if let scenes = info["scenesPath"] as? String {
            projectScenesURL = URL(fileURLWithPath: scenes)
        }
        if projectAssetsURL != nil {
            rescanAssetRoot()
        }
    }

    func createProject(at url: URL, name: String? = nil) {
        let projectName = name ?? url.lastPathComponent
        updateProjectAccess(from: url)
        let ok = CrescentEngineBridge.shared().createProject(path: url.path, name: projectName)
        if ok {
            addLog(.info, "Project created: \(projectName)")
            refreshProjectInfo()
        } else {
            addLog(.error, "Failed to create project: \(projectName)")
        }
    }

    func openProject(at url: URL) {
        var projectURL = url
        var isDir: ObjCBool = false
        let exists = FileManager.default.fileExists(atPath: projectURL.path, isDirectory: &isDir)
        if exists && isDir.boolValue {
            if !updateProjectAccess(from: projectURL) {
                addLog(.error, "Project folder access was not granted.")
                return
            }
            projectURL = projectURL.appendingPathComponent("Project.cproj")
        } else if projectURL.pathExtension.lowercased() != "cproj" {
            addLog(.error, "Select a project folder or Project.cproj")
            return
        } else {
            let parent = projectURL.deletingLastPathComponent()
            var accessGranted = updateProjectAccess(from: parent)
            if !accessGranted {
                if let grantedFolder = requestProjectFolderAccess() {
                    accessGranted = updateProjectAccess(from: grantedFolder)
                    projectURL = grantedFolder.appendingPathComponent("Project.cproj")
                }
            }
            if !accessGranted {
                addLog(.error, "Project folder access was not granted.")
                return
            }
        }
        let ok = CrescentEngineBridge.shared().openProject(path: projectURL.path)
        if ok {
            addLog(.info, "Project opened: \(projectURL.deletingLastPathComponent().lastPathComponent)")
            refreshProjectInfo()
        } else {
            addLog(.error, "Failed to open project: \(projectURL.path)")
        }
    }

    private func requestProjectFolderAccess() -> URL? {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Grant Access"
        let response = panel.runModal()
        return response == .OK ? panel.url : nil
    }

    @discardableResult
    private func updateProjectAccess(from url: URL?) -> Bool {
        guard let url else { return false }
        let accessed = url.startAccessingSecurityScopedResource()
        if accessed {
            if projectRootAccessActive, let oldURL = projectRootAccessURL {
                oldURL.stopAccessingSecurityScopedResource()
            }
            projectRootAccessActive = true
            projectRootAccessURL = url
        } else if !projectRootAccessActive {
            projectRootAccessURL = nil
        }
        return accessed
    }

    func pickAssetRoot() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Select"
        let response = panel.runModal()
        if response == .OK, let url = panel.url {
            setAssetRoot(from: url)
        }
    }
    
    func rescanAssetRoot() {
        if let assetsRoot = projectAssetsURL {
            var scanned = scanAssets(in: assetsRoot)
            if let scenesRoot = projectScenesURL {
                scanned.append(contentsOf: scanAssets(in: scenesRoot))
            }
            assets = scanned.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
            refreshAssetModificationCache()
            return
        }
        guard let root = assetRootURL else {
            assets = []
            assetModDates.removeAll()
            return
        }
        CrescentEngineBridge.shared().setAssetRoot(path: root.path)
        assets = scanAssets(in: root)
        refreshAssetModificationCache()
    }

    private func startHotReloadTimer() {
        hotReloadCancellable?.cancel()
        hotReloadCancellable = Timer.publish(every: 2.0, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                self?.pollAssetChanges()
            }
    }

    private func refreshAssetModificationCache() {
        assetModDates.removeAll()
        for asset in assets {
            if let date = modificationDate(for: asset.path) {
                assetModDates[asset.path] = date
            }
        }
    }

    private func updateAssetModificationCache(for asset: AssetInfo) {
        if let date = modificationDate(for: asset.path) {
            assetModDates[asset.path] = date
        }
    }

    private func modificationDate(for path: String) -> Date? {
        let attrs = try? FileManager.default.attributesOfItem(atPath: path)
        return attrs?[.modificationDate] as? Date
    }

    private func shouldAutoReimportAsset(at path: String, type: AssetInfo.AssetType) -> Bool {
        guard type == .texture else {
            return true
        }

        let normalizedPath = URL(fileURLWithPath: path).standardizedFileURL.path
        let generatedMarker = "/Generated/TerrainControl/"
        if normalizedPath.contains(generatedMarker) && normalizedPath.hasSuffix("_control.png") {
            return false
        }
        return true
    }

    private func pollAssetChanges() {
        if !autoReimportAssets || isPlaying {
            return
        }
        if assets.isEmpty {
            return
        }
        for asset in assets {
            switch asset.type {
            case .model, .texture, .hdri:
                break
            default:
                continue
            }
            if !shouldAutoReimportAsset(at: asset.path, type: asset.type) {
                if let date = modificationDate(for: asset.path) {
                    assetModDates[asset.path] = date
                }
                continue
            }
            guard let date = modificationDate(for: asset.path) else {
                assetModDates.removeValue(forKey: asset.path)
                continue
            }
            if let previous = assetModDates[asset.path] {
                if date > previous {
                    assetModDates[asset.path] = date
                    handleAssetModified(asset)
                }
            } else {
                assetModDates[asset.path] = date
            }
        }
    }

    private func handleAssetModified(_ asset: AssetInfo) {
        let meta = CrescentEngineBridge.shared().getAssetMeta(path: asset.path) as? [String: Any] ?? [:]
        let guid = meta["guid"] as? String ?? ""
        if guid.isEmpty {
            return
        }
        let bridge = CrescentEngineBridge.shared()
        let ok: Bool
        switch asset.type {
        case .model:
            ok = bridge.reimportModelAsset(guid: guid)
        case .texture:
            ok = bridge.reimportTextureAsset(guid: guid)
        case .hdri:
            ok = bridge.reimportHdriAsset(guid: guid)
        default:
            return
        }
        addLog(ok ? .info : .warning, ok ? "Auto reimported: \(asset.name)" : "Auto reimport skipped: \(asset.name)")
    }
    
    func importModel(from url: URL, options overrideOptions: ModelImportOptions? = nil) {
        let options = overrideOptions ?? modelImportOptions
        var resolvedURL = url
        let accessed = resolvedURL.startAccessingSecurityScopedResource()
        if accessed {
            resolvedURL = resolvedURL.standardizedFileURL
        }

        var path = resolvedURL.path
        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "model")
        if !imported.isEmpty {
            path = imported
        }
        if !FileManager.default.fileExists(atPath: path) {
            addLog(.error, "Model not found at path: \(path)")
        } else {
            let ok = CrescentEngineBridge.shared().importModel(path: path, options: options.toDictionary())
            if ok {
                addLog(.info, "Imported model: \(resolvedURL.lastPathComponent)")
                trackAsset(name: resolvedURL.lastPathComponent, path: path, type: .model)
            } else {
                addLog(.error, "Model import failed: \(resolvedURL.lastPathComponent)")
            }
        }
        
        if accessed {
            resolvedURL.stopAccessingSecurityScopedResource()
        }
        refreshEntityList()
    }

    func togglePlay() {
        let bridge = CrescentEngineBridge.shared()
        if isPlaying {
            bridge.exitPlayMode()
            isPlaying = false
            if isPaused {
                isPaused = false
                bridge.setPaused(false)
            }
            addLog(.info, "Exited Play Mode")
        } else {
            if terrainPaintEnabled {
                terrainPaintEnabled = false
                bridge.endTerrainPaint()
            }
            bridge.enterPlayMode()
            isPlaying = true
            isPaused = false
            bridge.setPaused(false)
            bridge.setTimeScale(timeScale)
            addLog(.info, "Entered Play Mode")
        }
        refreshEntityList()
    }

    func togglePause() {
        guard isPlaying else { return }
        let bridge = CrescentEngineBridge.shared()
        isPaused.toggle()
        bridge.setPaused(isPaused)
        addLog(.info, isPaused ? "Paused" : "Resumed")
    }

    func applyTimeScale() {
        if isPlaying {
            CrescentEngineBridge.shared().setTimeScale(timeScale)
        }
    }

    func setViewMode(_ mode: ViewMode) {
        if viewMode == mode {
            return
        }
        viewMode = mode
        if mode != .scene && terrainPaintEnabled {
            terrainPaintEnabled = false
            CrescentEngineBridge.shared().endTerrainPaint()
        }
        CrescentEngineBridge.shared().setViewMode(Int32(mode.rawValue))
    }

    func saveScene() {
        if let url = sceneURL {
            saveScene(to: url)
        } else {
            saveSceneAs()
        }
    }

    func saveSceneAs() {
        let panel = NSSavePanel()
        panel.allowedFileTypes = ["cscene"]
        panel.nameFieldStringValue = sceneName.isEmpty ? "Scene.cscene" : "\(sceneName).cscene"
        if let scenesURL = projectScenesURL {
            panel.directoryURL = scenesURL
        }
        panel.prompt = "Save"
        let response = panel.runModal()
        if response == .OK, let url = panel.url {
            saveScene(to: url)
        }
    }

    private func saveScene(to url: URL) {
        var resolvedURL = url
        let accessed = resolvedURL.startAccessingSecurityScopedResource()
        if accessed {
            resolvedURL = resolvedURL.standardizedFileURL
        }
        let ok = CrescentEngineBridge.shared().saveScene(path: resolvedURL.path)
        if ok {
            sceneURL = resolvedURL
            sceneName = resolvedURL.deletingPathExtension().lastPathComponent
            addLog(.info, "Saved scene: \(resolvedURL.lastPathComponent)")
        } else {
            addLog(.error, "Failed to save scene: \(resolvedURL.lastPathComponent)")
        }
        if accessed {
            resolvedURL.stopAccessingSecurityScopedResource()
        }
    }

    private func currentSceneRelativePathForBuild() -> String? {
        guard let sceneURL, let projectRootURL else {
            return nil
        }
        let rootPath = projectRootURL.standardizedFileURL.path
        let scenePath = sceneURL.standardizedFileURL.path
        guard scenePath.hasPrefix(rootPath) else {
            return nil
        }

        var relative = String(scenePath.dropFirst(rootPath.count))
        if relative.hasPrefix("/") {
            relative.removeFirst()
        }
        return relative.isEmpty ? nil : relative
    }

    @discardableResult
    private func updateBuildStartupScene(projectFileURL: URL, relativeScenePath: String) -> Bool {
        do {
            let data = try Data(contentsOf: projectFileURL)
            guard var project = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                addLog(.warning, "Build will use existing startup scene because Project.cproj could not be parsed.")
                return false
            }

            let existing = project["startupScene"] as? String ?? ""
            if existing == relativeScenePath {
                return true
            }

            project["startupScene"] = relativeScenePath
            let encoded = try JSONSerialization.data(withJSONObject: project, options: [.prettyPrinted, .sortedKeys])
            var output = encoded
            if let newline = "\n".data(using: .utf8) {
                output.append(newline)
            }
            try output.write(to: projectFileURL, options: .atomic)
            addLog(.info, "Build startup scene: \(relativeScenePath)")
            return true
        } catch {
            addLog(.warning, "Failed to set current scene as startup for build: \(error.localizedDescription)")
            return false
        }
    }

    func buildGame(configuration: String = "Debug") {
        guard !isBuildingGame else { return }
        guard hasProject, let projectRootURL else {
            addLog(.error, "Open a project before building a game.")
            return
        }

        let projectFileURL = projectRootURL.appendingPathComponent("Project.cproj")
        guard FileManager.default.fileExists(atPath: projectFileURL.path) else {
            addLog(.error, "Project.cproj is missing in the project root.")
            return
        }

        let scriptURL = Self.repositoryRootURL.appendingPathComponent("scripts/build_game.py")
        guard FileManager.default.fileExists(atPath: scriptURL.path) else {
            addLog(.error, "Build script is missing: \(scriptURL.path)")
            return
        }

        if sceneURL != nil {
            saveScene()
        }

        if let relativeScenePath = currentSceneRelativePathForBuild() {
            _ = updateBuildStartupScene(projectFileURL: projectFileURL, relativeScenePath: relativeScenePath)
        } else if sceneURL != nil {
            addLog(.warning, "Current scene is outside the project root. Build will use the configured startup scene.")
        }

        isBuildingGame = true
        dockCollapsed = false
        dockTab = .console
        showConsole = true
        addLog(.info, "Building game (\(configuration), cooked textures + baked lighting)...")

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        process.arguments = [
            "python3",
            scriptURL.path,
            projectFileURL.path,
            "--configuration",
            configuration,
            "--encode-textures"
        ]
        process.currentDirectoryURL = Self.repositoryRootURL

        let outputPipe = Pipe()
        process.standardOutput = outputPipe
        process.standardError = outputPipe

        DispatchQueue.global(qos: .userInitiated).async {
            let outputData = NSMutableData()
            var pendingOutput = ""
            outputPipe.fileHandleForReading.readabilityHandler = { handle in
                let data = handle.availableData
                if !data.isEmpty {
                    outputData.append(data)
                    if let text = String(data: data, encoding: .utf8), !text.isEmpty {
                        pendingOutput += text.replacingOccurrences(of: "\r\n", with: "\n")
                            .replacingOccurrences(of: "\r", with: "\n")
                        let lines = pendingOutput.components(separatedBy: "\n")
                        pendingOutput = lines.last ?? ""
                        let completed = lines.dropLast().map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
                            .filter { !$0.isEmpty }
                        if !completed.isEmpty {
                            DispatchQueue.main.async {
                                for line in completed {
                                    self.addLog(Self.logTypeForBuildLine(line), line)
                                }
                            }
                        }
                    }
                }
            }

            do {
                try process.run()
                process.waitUntilExit()
            } catch {
                outputPipe.fileHandleForReading.readabilityHandler = nil
                DispatchQueue.main.async {
                    self.isBuildingGame = false
                    self.addLog(.error, "Failed to start build: \(error.localizedDescription)")
                }
                return
            }

            outputPipe.fileHandleForReading.readabilityHandler = nil
            let output = String(data: outputData as Data, encoding: .utf8) ?? ""
            let lines = output.split(whereSeparator: \.isNewline).map(String.init)
            let packagedPrefix = "Packaged app: "
            let packagedPath = lines.last(where: { $0.hasPrefix(packagedPrefix) }).map {
                String($0.dropFirst(packagedPrefix.count))
            }

            DispatchQueue.main.async {
                let trailing = pendingOutput.trimmingCharacters(in: .whitespacesAndNewlines)
                if !trailing.isEmpty {
                    self.addLog(Self.logTypeForBuildLine(trailing), trailing)
                }
                self.isBuildingGame = false
                if process.terminationStatus == 0, let packagedPath {
                    let appURL = URL(fileURLWithPath: packagedPath)
                    self.lastBuiltAppURL = appURL
                    self.addLog(.info, "Build completed: \(appURL.lastPathComponent)")
                    NSWorkspace.shared.activateFileViewerSelecting([appURL])
                } else {
                    let tail = lines.suffix(12).joined(separator: "\n")
                    let detail = tail.isEmpty ? "Unknown error" : tail
                    self.addLog(.error, "Build failed:\n\(detail)")
                }
            }
        }
    }

    func bakeLighting() {
        guard hasProject else {
            addLog(.error, "Open a project before baking lighting.")
            return
        }

        dockCollapsed = false
        dockTab = .console
        showConsole = true
        addLog(.info, "Baking static lighting...")

        let result = CrescentEngineBridge.shared().bakeSceneStaticLighting() as? [String: Any] ?? [:]
        let atlasCount = (result["atlasCount"] as? NSNumber)?.intValue ?? 0
        let rendererCount = (result["bakedRendererCount"] as? NSNumber)?.intValue ?? 0
        let texelCount = (result["bakedTexelCount"] as? NSNumber)?.intValue ?? 0
        let lightCount = (result["bakedLightCount"] as? NSNumber)?.intValue ?? 0
        let staticGeometryCount = (result["staticGeometryRendererCount"] as? NSNumber)?.intValue ?? 0
        let layoutRendererCount = (result["layoutRendererCount"] as? NSNumber)?.intValue ?? 0
        let layoutSkippedCount = (result["layoutSkippedRendererCount"] as? NSNumber)?.intValue ?? 0
        let generatedUVRendererCount = (result["generatedUVRendererCount"] as? NSNumber)?.intValue ?? 0
        let reusedUVRendererCount = (result["reusedUVRendererCount"] as? NSNumber)?.intValue ?? 0

        if lightCount == 0 {
            addLog(.warning, "No lights are marked to contribute to static bake.")
        } else if atlasCount == 0 || rendererCount == 0 {
            addLog(.warning, "Static bake produced no output. Static meshes: \(staticGeometryCount), layout-ready: \(layoutRendererCount), skipped: \(layoutSkippedCount), reused UVs: \(reusedUVRendererCount), generated UVs: \(generatedUVRendererCount). Mark a mesh as Material > Static Lighting > Static Geometry.")
        } else {
            addLog(.info, "Baked \(atlasCount) atlases for \(rendererCount) renderers from \(lightCount) static lights (\(texelCount) texels).")
        }

        refreshEntityList()
        if sceneURL != nil {
            saveScene()
        }
    }

    func loadScene() {
        let panel = NSOpenPanel()
        panel.allowedFileTypes = ["cscene"]
        panel.allowsMultipleSelection = false
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        if let scenesURL = projectScenesURL {
            panel.directoryURL = scenesURL
        }
        panel.prompt = "Open"
        let response = panel.runModal()
        if response == .OK, let url = panel.url {
            loadScene(from: url)
        }
    }

    func openScene(at url: URL) {
        loadScene(from: url)
    }

    private func loadScene(from url: URL) {
        var resolvedURL = url
        let accessed = resolvedURL.startAccessingSecurityScopedResource()
        if accessed {
            resolvedURL = resolvedURL.standardizedFileURL
        }
        let ok = CrescentEngineBridge.shared().loadScene(path: resolvedURL.path)
        if ok {
            sceneURL = resolvedURL
            sceneName = resolvedURL.deletingPathExtension().lastPathComponent
            addLog(.info, "Loaded scene: \(resolvedURL.lastPathComponent)")
        } else {
            addLog(.error, "Failed to load scene: \(resolvedURL.lastPathComponent)")
        }
        if accessed {
            resolvedURL.stopAccessingSecurityScopedResource()
        }
        refreshEntityList()
    }

    func isModelFile(_ url: URL) -> Bool {
        modelExtensions.contains(url.pathExtension.lowercased())
    }
    
    private func trackAsset(name: String, path: String, type: AssetInfo.AssetType) {
        let relativePath = assetRelativePath(for: path)
        if let root = assetRootURL, !path.hasPrefix(root.path) {
            return
        }
        if assets.contains(where: { $0.path == path }) {
            return
        }
        let attributes = try? FileManager.default.attributesOfItem(atPath: path)
        let modified = attributes?[.modificationDate] as? Date
        let sizeBytes = (attributes?[.size] as? NSNumber)?.int64Value ?? 0
        assets.append(AssetInfo(name: name,
                                path: path,
                                relativePath: relativePath,
                                type: type,
                                modified: modified,
                                sizeBytes: sizeBytes))
        if let asset = assets.last {
            updateAssetModificationCache(for: asset)
        }
    }

    private func scanAssets(in root: URL) -> [AssetInfo] {
        var scanned: [AssetInfo] = []
        let fileManager = FileManager.default
        let resourceKeys: Set<URLResourceKey> = [
            .isRegularFileKey,
            .contentModificationDateKey,
            .fileSizeKey
        ]
        if let enumerator = fileManager.enumerator(at: root, includingPropertiesForKeys: Array(resourceKeys), options: [.skipsHiddenFiles, .skipsPackageDescendants]) {
            for case let fileURL as URL in enumerator {
                guard let values = try? fileURL.resourceValues(forKeys: resourceKeys),
                      values.isRegularFile == true else { continue }
                guard let type = assetType(for: fileURL) else { continue }
                let name = fileURL.lastPathComponent
                let relative = assetRelativePath(for: fileURL.path)
                let sizeBytes = Int64(values.fileSize ?? 0)
                scanned.append(AssetInfo(name: name,
                                         path: fileURL.path,
                                         relativePath: relative,
                                         type: type,
                                         modified: values.contentModificationDate,
                                         sizeBytes: sizeBytes))
            }
        }
        return scanned.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }
    
    private func assetType(for url: URL) -> AssetInfo.AssetType? {
        let ext = url.pathExtension.lowercased()
        if modelExtensions.contains(ext) {
            return .model
        }
        if hdriExtensions.contains(ext) {
            return .hdri
        }
        if textureExtensions.contains(ext) {
            return .texture
        }
        if audioExtensions.contains(ext) {
            return .audio
        }
        if materialExtensions.contains(ext) {
            return .material
        }
        if sceneExtensions.contains(ext) {
            return .scene
        }
        return nil
    }
    
    private func assetRelativePath(for path: String) -> String {
        let root = projectRootURL ?? assetRootURL
        guard let root else { return path }
        let rootPath = root.path
        if path.hasPrefix(rootPath) {
            let start = path.index(path.startIndex, offsetBy: rootPath.count)
            let trimmed = path[start...].trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            return trimmed.isEmpty ? root.lastPathComponent : trimmed
        }
        return path
    }
    
    private let modelExtensions: Set<String> = [
        "fbx", "obj", "gltf", "glb", "dae", "blend", "3ds",
        "stl", "ply", "x", "smd", "md5mesh", "md2", "md3", "ms3d", "lwo", "lws"
    ]
    
    private let textureExtensions: Set<String> = [
        "png", "jpg", "jpeg", "tga", "bmp", "gif", "tif", "tiff", "ktx", "ktx2", "dds", "cube"
    ]
    
    private let hdriExtensions: Set<String> = [
        "hdr", "exr"
    ]

    private let audioExtensions: Set<String> = [
        "wav", "mp3", "ogg", "flac"
    ]

    private let materialExtensions: Set<String> = [
        "cmat"
    ]

    private let sceneExtensions: Set<String> = [
        "cscene"
    ]
}
