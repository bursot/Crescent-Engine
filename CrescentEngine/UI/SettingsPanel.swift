import SwiftUI
import AppKit
import Combine

struct QualitySettings: Hashable {
    var shadowQuality: Int = 2
    var shadowResolution: Int = 2048
    var msaaSamples: Int = 4
    var anisotropy: Int = 8
    var renderScale: Double = 1.0
    var lodBias: Double = 0.0
    var textureQuality: Int = 2
    
    init() {}
    
    init(shadowQuality: Int,
         shadowResolution: Int,
         msaaSamples: Int,
         anisotropy: Int,
         renderScale: Double,
         lodBias: Double,
         textureQuality: Int) {
        self.shadowQuality = shadowQuality
        self.shadowResolution = shadowResolution
        self.msaaSamples = msaaSamples
        self.anisotropy = anisotropy
        self.renderScale = renderScale
        self.lodBias = lodBias
        self.textureQuality = textureQuality
    }
    
    init(from dict: [String: Any]) {
        shadowQuality = dict["shadowQuality"] as? Int ?? shadowQuality
        shadowResolution = dict["shadowResolution"] as? Int ?? shadowResolution
        msaaSamples = dict["msaaSamples"] as? Int ?? msaaSamples
        anisotropy = dict["anisotropy"] as? Int ?? anisotropy
        renderScale = dict["renderScale"] as? Double ?? renderScale
        lodBias = dict["lodBias"] as? Double ?? lodBias
        textureQuality = dict["textureQuality"] as? Int ?? textureQuality
    }
    
    func toDictionary() -> [String: Any] {
        [
            "shadowQuality": shadowQuality,
            "shadowResolution": shadowResolution,
            "msaaSamples": msaaSamples,
            "anisotropy": anisotropy,
            "renderScale": renderScale,
            "lodBias": lodBias,
            "textureQuality": textureQuality
        ]
    }
}

struct RenderProfileItem: Identifiable, Hashable {
    let id = UUID()
    var name: String
    var quality: QualitySettings
}

struct QualityPresetItem: Identifiable, Hashable {
    let id = UUID()
    var name: String
    var quality: QualitySettings
}

struct InputBindingItem: Identifiable, Hashable {
    let id = UUID()
    var action: String
    var key: String
    var mouseButton: String
    var scale: Double
    var invert: Bool
}

final class ProjectSettingsViewModel: ObservableObject {
    @Published var defaultRenderProfile: String = "High"
    @Published var buildTarget: String = "macOS"
    @Published var assetPaths: [String] = []
    @Published var renderProfiles: [RenderProfileItem] = []
    @Published var qualityPresets: [QualityPresetItem] = []
    @Published var inputBindings: [InputBindingItem] = []
    private weak var editorState: EditorState?
    private var isLoading = false
    
    init(editorState: EditorState) {
        self.editorState = editorState
        refresh()
    }
    
    func refresh() {
        isLoading = true
        defer { isLoading = false }
        let dict = CrescentEngineBridge.shared().getProjectSettings() as? [String: Any] ?? [:]
        defaultRenderProfile = dict["defaultRenderProfile"] as? String ?? defaultRenderProfile
        buildTarget = dict["buildTarget"] as? String ?? buildTarget
        assetPaths = dict["assetPaths"] as? [String] ?? assetPaths
        if assetPaths.isEmpty {
            assetPaths = ["Assets"]
        }
        
        if let profiles = dict["renderProfiles"] as? [[String: Any]] {
            renderProfiles = profiles.map { entry in
                let name = entry["name"] as? String ?? "Profile"
                let quality = QualitySettings(from: entry["quality"] as? [String: Any] ?? [:])
                return RenderProfileItem(name: name, quality: quality)
            }
        }
        if renderProfiles.isEmpty {
            renderProfiles = defaultRenderProfiles()
        }
        
        if let presets = dict["qualityPresets"] as? [[String: Any]] {
            qualityPresets = presets.map { entry in
                let name = entry["name"] as? String ?? "Preset"
                let quality = QualitySettings(from: entry["quality"] as? [String: Any] ?? [:])
                return QualityPresetItem(name: name, quality: quality)
            }
        }
        if qualityPresets.isEmpty {
            qualityPresets = defaultQualityPresets()
        }
        
        if let bindings = dict["inputBindings"] as? [[String: Any]] {
            inputBindings = bindings.map { entry in
                InputBindingItem(
                    action: entry["action"] as? String ?? "",
                    key: entry["key"] as? String ?? "",
                    mouseButton: entry["mouseButton"] as? String ?? "",
                    scale: entry["scale"] as? Double ?? 1.0,
                    invert: entry["invert"] as? Bool ?? false
                )
            }
        }
        if inputBindings.isEmpty {
            inputBindings = defaultInputBindings()
        }
        
        normalizeDefaultProfile()
    }
    
    func apply() {
        guard !isLoading else { return }
        normalizeDefaultProfile()
        let info: [String: Any] = [
            "defaultRenderProfile": defaultRenderProfile,
            "buildTarget": buildTarget,
            "assetPaths": assetPaths,
            "renderProfiles": renderProfiles.map { ["name": $0.name, "quality": $0.quality.toDictionary()] },
            "qualityPresets": qualityPresets.map { ["name": $0.name, "quality": $0.quality.toDictionary()] },
            "inputBindings": inputBindings.map {
                [
                    "action": $0.action,
                    "key": $0.key,
                    "mouseButton": $0.mouseButton,
                    "scale": $0.scale,
                    "invert": $0.invert
                ]
            }
        ]
        CrescentEngineBridge.shared().setProjectSettings(settings: info)
    }
    
    func addAssetPath() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Add Path"
        let response = panel.runModal()
        guard response == .OK, let url = panel.url else { return }
        let path = makeRelativePath(url)
        if assetPaths.contains(path) {
            return
        }
        assetPaths.append(path)
        apply()
    }
    
    func removeAssetPaths(at offsets: IndexSet) {
        assetPaths.remove(atOffsets: offsets)
        if assetPaths.isEmpty {
            assetPaths = ["Assets"]
        }
        apply()
    }
    
    func addRenderProfile() {
        renderProfiles.append(RenderProfileItem(name: "Custom", quality: QualitySettings()))
        normalizeDefaultProfile()
        apply()
    }
    
    func removeRenderProfile(at index: Int) {
        guard renderProfiles.indices.contains(index) else { return }
        let removedName = renderProfiles[index].name
        renderProfiles.remove(at: index)
        if removedName == defaultRenderProfile {
            defaultRenderProfile = renderProfiles.first?.name ?? "High"
        }
        normalizeDefaultProfile()
        apply()
    }
    
    func addQualityPreset() {
        qualityPresets.append(QualityPresetItem(name: "Custom", quality: QualitySettings()))
        apply()
    }
    
    func removeQualityPreset(at index: Int) {
        guard qualityPresets.indices.contains(index) else { return }
        qualityPresets.remove(at: index)
        apply()
    }
    
    func addInputBinding() {
        inputBindings.append(InputBindingItem(action: "Action", key: "", mouseButton: "", scale: 1.0, invert: false))
        apply()
    }
    
    func removeInputBinding(at index: Int) {
        guard inputBindings.indices.contains(index) else { return }
        inputBindings.remove(at: index)
        apply()
    }
    
    func applySceneQuality(_ quality: QualitySettings, overrideProject: Bool = true) {
        var payload = quality.toDictionary()
        payload["overrideProject"] = overrideProject
        CrescentEngineBridge.shared().setSceneSettings(settings: ["quality": payload])
        editorState?.addLog(.info, "Applied quality to active scene")
    }
    
    private func makeRelativePath(_ url: URL) -> String {
        guard let root = editorState?.projectRootURL else {
            return url.path
        }
        let rootPath = root.standardizedFileURL.path
        let path = url.standardizedFileURL.path
        if path.hasPrefix(rootPath) {
            let start = path.index(path.startIndex, offsetBy: rootPath.count)
            let trimmed = path[start...].trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            return trimmed.isEmpty ? root.lastPathComponent : trimmed
        }
        return path
    }
    
    private func normalizeDefaultProfile() {
        if renderProfiles.isEmpty {
            renderProfiles = defaultRenderProfiles()
        }
        if !renderProfiles.contains(where: { $0.name == defaultRenderProfile }) {
            defaultRenderProfile = renderProfiles.first?.name ?? "High"
        }
    }
    
    private func defaultRenderProfiles() -> [RenderProfileItem] {
        [
            RenderProfileItem(name: "Low", quality: QualitySettings(shadowQuality: 0, shadowResolution: 512, msaaSamples: 1, anisotropy: 2, renderScale: 0.85, lodBias: 1.0, textureQuality: 0)),
            RenderProfileItem(name: "Medium", quality: QualitySettings(shadowQuality: 1, shadowResolution: 1024, msaaSamples: 2, anisotropy: 4, renderScale: 1.0, lodBias: 0.5, textureQuality: 1)),
            RenderProfileItem(name: "High", quality: QualitySettings(shadowQuality: 2, shadowResolution: 2048, msaaSamples: 4, anisotropy: 8, renderScale: 1.0, lodBias: 0.0, textureQuality: 2)),
            RenderProfileItem(name: "Ultra", quality: QualitySettings(shadowQuality: 3, shadowResolution: 4096, msaaSamples: 8, anisotropy: 16, renderScale: 1.0, lodBias: -0.5, textureQuality: 3))
        ]
    }
    
    private func defaultQualityPresets() -> [QualityPresetItem] {
        [
            QualityPresetItem(name: "Low", quality: QualitySettings(shadowQuality: 0, shadowResolution: 512, msaaSamples: 1, anisotropy: 2, renderScale: 0.85, lodBias: 1.0, textureQuality: 0)),
            QualityPresetItem(name: "Medium", quality: QualitySettings(shadowQuality: 1, shadowResolution: 1024, msaaSamples: 2, anisotropy: 4, renderScale: 1.0, lodBias: 0.5, textureQuality: 1)),
            QualityPresetItem(name: "High", quality: QualitySettings(shadowQuality: 2, shadowResolution: 2048, msaaSamples: 4, anisotropy: 8, renderScale: 1.0, lodBias: 0.0, textureQuality: 2)),
            QualityPresetItem(name: "Ultra", quality: QualitySettings(shadowQuality: 3, shadowResolution: 4096, msaaSamples: 8, anisotropy: 16, renderScale: 1.0, lodBias: -0.5, textureQuality: 3))
        ]
    }
    
    private func defaultInputBindings() -> [InputBindingItem] {
        [
            InputBindingItem(action: "MoveForward", key: "W", mouseButton: "", scale: 1.0, invert: false),
            InputBindingItem(action: "MoveBackward", key: "S", mouseButton: "", scale: 1.0, invert: false),
            InputBindingItem(action: "MoveLeft", key: "A", mouseButton: "", scale: 1.0, invert: false),
            InputBindingItem(action: "MoveRight", key: "D", mouseButton: "", scale: 1.0, invert: false),
            InputBindingItem(action: "MoveUp", key: "E", mouseButton: "", scale: 1.0, invert: false),
            InputBindingItem(action: "MoveDown", key: "Q", mouseButton: "", scale: 1.0, invert: false),
            InputBindingItem(action: "Jump", key: "Space", mouseButton: "", scale: 1.0, invert: false)
        ]
    }
}

final class SceneSettingsViewModel: ObservableObject {
    @Published var skyboxPath: String = "Builtin Sky"
    @Published var skyMode: Int = 0
    @Published var exposureEV: Double = 0.0
    @Published var iblIntensity: Double = 1.0
    @Published var skyIntensity: Double = 1.0
    @Published var saturation: Double = 1.0
    @Published var contrast: Double = 1.0
    @Published var blurLevel: Double = 0.0
    @Published var rotation: [Double] = [0, 0, 0]
    @Published var tint: Color = .white
    @Published var skyboxVisible: Bool = true
    @Published var autoSunColor: Bool = true
    
    @Published var fogEnabled: Bool = false
    @Published var fogColor: Color = Color(red: 0.6, green: 0.6, blue: 0.65)
    @Published var fogDensity: Double = 0.02
    @Published var fogStart: Double = 0.0
    @Published var fogEnd: Double = 200.0
    @Published var heightFog: Bool = false
    @Published var fogHeight: Double = 0.0
    @Published var fogHeightFalloff: Double = 0.2
    @Published var fogScattering: Double = 1.0
    @Published var fogAnisotropy: Double = 0.4
    @Published var fogHistoryWeight: Double = 0.9
    @Published var fogQuality: Int = 1
    
    @Published var postEnabled: Bool = true
    @Published var bloom: Bool = false
    @Published var bloomIntensity: Double = 0.8
    @Published var bloomThreshold: Double = 1.0
    @Published var toneMapping: Bool = true
    @Published var toneMappingMode: Int = 1
    @Published var colorGrading: Bool = false
    @Published var postSaturation: Double = 1.0
    @Published var postContrast: Double = 1.0
    @Published var colorGradingIntensity: Double = 1.0
    @Published var colorGradingLUTPath: String = ""
    @Published var vignette: Bool = false
    @Published var vignetteIntensity: Double = 0.3
    @Published var filmGrain: Bool = false
    @Published var filmGrainIntensity: Double = 0.15
    @Published var ssao: Bool = false
    @Published var ssaoRadius: Double = 0.5
    @Published var ssaoStrength: Double = 1.0
    @Published var ssr: Bool = false
    @Published var ssrMaxRoughness: Double = 0.6
    @Published var ssrThickness: Double = 0.1
    @Published var taa: Bool = false
    @Published var taaSharpness: Double = 0.5
    @Published var fxaa: Bool = false
    @Published var motionBlur: Bool = false
    @Published var motionBlurStrength: Double = 0.5
    @Published var dof: Bool = false
    @Published var dofFocusDistance: Double = 5.0
    @Published var dofAperture: Double = 2.8
    
    @Published var qualityOverride: Bool = false
    @Published var shadowQuality: Int = 2
    @Published var shadowResolution: Int = 1024
    @Published var msaaSamples: Int = 1
    @Published var anisotropy: Int = 8
    @Published var renderScale: Double = 1.0
    @Published var lodBias: Double = 0.0
    @Published var textureQuality: Int = 2
    
    private weak var editorState: EditorState?
    private var isLoading = false
    
    init(editorState: EditorState) {
        self.editorState = editorState
        refresh()
    }
    
    func refresh() {
        isLoading = true
        defer { isLoading = false }
        let dict = CrescentEngineBridge.shared().getSceneSettings() as? [String: Any] ?? [:]
        if let env = dict["environment"] as? [String: Any] {
            skyboxPath = env["skyboxPath"] as? String ?? skyboxPath
            exposureEV = env["exposureEV"] as? Double ?? exposureEV
            iblIntensity = env["iblIntensity"] as? Double ?? iblIntensity
            skyIntensity = env["skyIntensity"] as? Double ?? skyIntensity
            saturation = env["saturation"] as? Double ?? saturation
            contrast = env["contrast"] as? Double ?? contrast
            blurLevel = env["blurLevel"] as? Double ?? blurLevel
            if let rot = env["rotation"] as? [NSNumber], rot.count >= 3 {
                rotation = [rot[0].doubleValue, rot[1].doubleValue, rot[2].doubleValue]
            }
            if let tintValues = env["tint"] as? [NSNumber], tintValues.count >= 3 {
                tint = Color(red: tintValues[0].doubleValue,
                             green: tintValues[1].doubleValue,
                             blue: tintValues[2].doubleValue)
            }
            skyboxVisible = env["skyboxVisible"] as? Bool ?? skyboxVisible
            skyMode = env["skyMode"] as? Int ?? skyMode
            autoSunColor = env["autoSunColor"] as? Bool ?? autoSunColor
        }
        if let fog = dict["fog"] as? [String: Any] {
            fogEnabled = fog["enabled"] as? Bool ?? fogEnabled
            if let color = fog["color"] as? [NSNumber], color.count >= 3 {
                fogColor = Color(red: color[0].doubleValue,
                                 green: color[1].doubleValue,
                                 blue: color[2].doubleValue)
            }
            fogDensity = fog["density"] as? Double ?? fogDensity
            fogStart = fog["startDistance"] as? Double ?? fogStart
            fogEnd = fog["endDistance"] as? Double ?? fogEnd
            heightFog = fog["heightFog"] as? Bool ?? heightFog
            fogHeight = fog["height"] as? Double ?? fogHeight
            fogHeightFalloff = fog["heightFalloff"] as? Double ?? fogHeightFalloff
            fogScattering = fog["volumetricScattering"] as? Double ?? fogScattering
            fogAnisotropy = fog["volumetricAnisotropy"] as? Double ?? fogAnisotropy
            fogHistoryWeight = fog["volumetricHistoryWeight"] as? Double ?? fogHistoryWeight
            fogQuality = fog["volumetricQuality"] as? Int ?? fogQuality
        }
        if let post = dict["postProcess"] as? [String: Any] {
            postEnabled = post["enabled"] as? Bool ?? postEnabled
            bloom = post["bloom"] as? Bool ?? bloom
            bloomIntensity = post["bloomIntensity"] as? Double ?? bloomIntensity
            bloomThreshold = post["bloomThreshold"] as? Double ?? bloomThreshold
            toneMapping = post["toneMapping"] as? Bool ?? toneMapping
            toneMappingMode = post["toneMappingMode"] as? Int ?? toneMappingMode
            colorGrading = post["colorGrading"] as? Bool ?? colorGrading
            colorGradingIntensity = post["colorGradingIntensity"] as? Double ?? colorGradingIntensity
            colorGradingLUTPath = post["colorGradingLUT"] as? String ?? colorGradingLUTPath
            postSaturation = post["saturation"] as? Double ?? postSaturation
            postContrast = post["contrast"] as? Double ?? postContrast
            vignette = post["vignette"] as? Bool ?? vignette
            vignetteIntensity = post["vignetteIntensity"] as? Double ?? vignetteIntensity
            filmGrain = post["filmGrain"] as? Bool ?? filmGrain
            filmGrainIntensity = post["filmGrainIntensity"] as? Double ?? filmGrainIntensity
            ssao = post["ssao"] as? Bool ?? ssao
            ssaoRadius = post["ssaoRadius"] as? Double ?? ssaoRadius
            ssaoStrength = post["ssaoStrength"] as? Double ?? ssaoStrength
            ssr = post["ssr"] as? Bool ?? ssr
            ssrMaxRoughness = post["ssrMaxRoughness"] as? Double ?? ssrMaxRoughness
            ssrThickness = post["ssrThickness"] as? Double ?? ssrThickness
            taa = post["taa"] as? Bool ?? taa
            taaSharpness = post["taaSharpness"] as? Double ?? taaSharpness
            fxaa = post["fxaa"] as? Bool ?? fxaa
            motionBlur = post["motionBlur"] as? Bool ?? motionBlur
            motionBlurStrength = post["motionBlurStrength"] as? Double ?? motionBlurStrength
            dof = post["depthOfField"] as? Bool ?? dof
            dofFocusDistance = post["dofFocusDistance"] as? Double ?? dofFocusDistance
            dofAperture = post["dofAperture"] as? Double ?? dofAperture
        }
        if let quality = dict["quality"] as? [String: Any] {
            qualityOverride = quality["overrideProject"] as? Bool ?? qualityOverride
            shadowQuality = quality["shadowQuality"] as? Int ?? shadowQuality
            shadowResolution = quality["shadowResolution"] as? Int ?? shadowResolution
            msaaSamples = quality["msaaSamples"] as? Int ?? msaaSamples
            anisotropy = quality["anisotropy"] as? Int ?? anisotropy
            renderScale = quality["renderScale"] as? Double ?? renderScale
            lodBias = quality["lodBias"] as? Double ?? lodBias
            textureQuality = quality["textureQuality"] as? Int ?? textureQuality
        }
    }
    
    func apply() {
        guard !isLoading else { return }
        let tintValues = components(from: tint)
        let fogValues = components(from: fogColor)
        let info: [String: Any] = [
            "environment": [
                "skyboxPath": skyboxPath,
                "skyMode": skyMode,
                "exposureEV": exposureEV,
                "iblIntensity": iblIntensity,
                "skyIntensity": skyIntensity,
                "saturation": saturation,
                "contrast": contrast,
                "blurLevel": blurLevel,
                "rotation": rotation,
                "tint": [tintValues.r, tintValues.g, tintValues.b],
                "skyboxVisible": skyboxVisible,
                "autoSunColor": autoSunColor
            ],
            "fog": [
                "enabled": fogEnabled,
                "color": [fogValues.r, fogValues.g, fogValues.b],
                "density": fogDensity,
                "startDistance": fogStart,
                "endDistance": fogEnd,
                "heightFog": heightFog,
                "height": fogHeight,
                "heightFalloff": fogHeightFalloff,
                "volumetricScattering": fogScattering,
                "volumetricAnisotropy": fogAnisotropy,
                "volumetricHistoryWeight": fogHistoryWeight,
                "volumetricQuality": fogQuality
            ],
            "postProcess": [
                "enabled": postEnabled,
                "bloom": bloom,
                "bloomIntensity": bloomIntensity,
                "bloomThreshold": bloomThreshold,
                "toneMapping": toneMapping,
                "toneMappingMode": toneMappingMode,
                "colorGrading": colorGrading,
                "colorGradingIntensity": colorGradingIntensity,
                "colorGradingLUT": colorGradingLUTPath,
                "saturation": postSaturation,
                "contrast": postContrast,
                "vignette": vignette,
                "vignetteIntensity": vignetteIntensity,
                "filmGrain": filmGrain,
                "filmGrainIntensity": filmGrainIntensity,
                "ssao": ssao,
                "ssaoRadius": ssaoRadius,
                "ssaoStrength": ssaoStrength,
                "ssr": ssr,
                "ssrMaxRoughness": ssrMaxRoughness,
                "ssrThickness": ssrThickness,
                "taa": taa,
                "taaSharpness": taaSharpness,
                "fxaa": fxaa,
                "motionBlur": motionBlur,
                "motionBlurStrength": motionBlurStrength,
                "depthOfField": dof,
                "dofFocusDistance": dofFocusDistance,
                "dofAperture": dofAperture
            ],
            "quality": [
                "overrideProject": qualityOverride,
                "shadowQuality": shadowQuality,
                "shadowResolution": shadowResolution,
                "msaaSamples": msaaSamples,
                "anisotropy": anisotropy,
                "renderScale": renderScale,
                "lodBias": lodBias,
                "textureQuality": textureQuality
            ]
        ]
        CrescentEngineBridge.shared().setSceneSettings(settings: info)
    }
    
    func loadSkybox() {
        let panel = NSOpenPanel()
        panel.allowedFileTypes = ["hdr", "exr"]
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Load"
        let response = panel.runModal()
        guard response == .OK, let url = panel.url else { return }
        var path = url.path
        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "hdri")
        if !imported.isEmpty {
            path = imported
        }
        skyboxPath = path
        apply()
        editorState?.addLog(.info, "Skybox set: \(url.lastPathComponent)")
    }
    
    func resetSkybox() {
        skyboxPath = "Builtin Sky"
        apply()
        editorState?.addLog(.info, "Skybox reset to builtin")
    }

    func loadColorGradingLUT() {
        let panel = NSOpenPanel()
        panel.allowedFileTypes = ["png", "jpg", "jpeg", "tga", "bmp", "cube"]
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Load"
        let response = panel.runModal()
        guard response == .OK, let url = panel.url else { return }
        var path = url.path
        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "texture")
        if !imported.isEmpty {
            path = imported
        }
        colorGradingLUTPath = path
        apply()
        editorState?.addLog(.info, "LUT set: \(url.lastPathComponent)")
    }

    func resetColorGradingLUT() {
        colorGradingLUTPath = ""
        apply()
        editorState?.addLog(.info, "LUT cleared")
    }
    
    private func components(from color: Color) -> (r: CGFloat, g: CGFloat, b: CGFloat) {
        let nsColor = NSColor(color)
        guard let rgb = nsColor.usingColorSpace(.deviceRGB) else {
            return (1, 1, 1)
        }
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        rgb.getRed(&r, green: &g, blue: &b, alpha: &a)
        return (r, g, b)
    }
}

struct SettingsPanel: View {
    @ObservedObject var editorState: EditorState
    @StateObject private var projectViewModel: ProjectSettingsViewModel
    @StateObject private var sceneViewModel: SceneSettingsViewModel
    
    init(editorState: EditorState) {
        self.editorState = editorState
        _projectViewModel = StateObject(wrappedValue: ProjectSettingsViewModel(editorState: editorState))
        _sceneViewModel = StateObject(wrappedValue: SceneSettingsViewModel(editorState: editorState))
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            ComponentSection(title: "Project Settings", icon: "folder.badge.gearshape") {
                ProjectSettingsPanel(
                    viewModel: projectViewModel,
                    editorState: editorState,
                    refreshSceneSettings: sceneViewModel.refresh
                )
            }
            
            ComponentSection(title: "Scene Settings", icon: "slider.horizontal.3") {
                SceneSettingsPanel(viewModel: sceneViewModel)
            }
        }
        .onAppear {
            projectViewModel.refresh()
            sceneViewModel.refresh()
        }
        .onChange(of: editorState.projectRootURL) { _ in
            projectViewModel.refresh()
            sceneViewModel.refresh()
        }
        .onChange(of: editorState.sceneURL) { _ in
            sceneViewModel.refresh()
        }
    }
}

private struct ProjectSettingsPanel: View {
    @ObservedObject var viewModel: ProjectSettingsViewModel
    @ObservedObject var editorState: EditorState
    let refreshSceneSettings: () -> Void
    
    private let buildTargets = ["macOS", "iOS", "iPadOS"]
    
    private var renderProfileNames: [String] {
        let names = viewModel.renderProfiles.map { $0.name }.filter { !$0.isEmpty }
        return names.isEmpty ? ["High"] : names
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            SettingsRow(title: "Project") {
                Text(editorState.projectName.isEmpty ? "Untitled Project" : editorState.projectName)
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
            }
            
            SettingsRow(title: "Default Render Profile") {
                Picker("", selection: $viewModel.defaultRenderProfile) {
                    ForEach(renderProfileNames, id: \.self) { profile in
                        Text(profile).tag(profile)
                    }
                }
                .labelsHidden()
                .frame(width: 160)
                .onChange(of: viewModel.defaultRenderProfile) { _ in
                    viewModel.apply()
                }
            }
            
            SettingsRow(title: "Build Target") {
                Picker("", selection: $viewModel.buildTarget) {
                    ForEach(buildTargets, id: \.self) { target in
                        Text(target).tag(target)
                    }
                }
                .labelsHidden()
                .frame(width: 160)
                .onChange(of: viewModel.buildTarget) { _ in
                    viewModel.apply()
                }
            }
            
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Asset Search Paths")
                        .font(EditorTheme.fontBodyMedium)
                    Spacer()
                    Button("Add") {
                        viewModel.addAssetPath()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
                .foregroundColor(EditorTheme.textPrimary)
                
                ForEach(viewModel.assetPaths, id: \.self) { path in
                    HStack(spacing: 6) {
                        Image(systemName: "folder")
                            .foregroundColor(EditorTheme.textMuted)
                        Text(path)
                            .font(EditorTheme.fontBody)
                            .foregroundColor(EditorTheme.textPrimary)
                            .lineLimit(1)
                        Spacer()
                        Button(action: {
                            if let index = viewModel.assetPaths.firstIndex(of: path) {
                                viewModel.removeAssetPaths(at: IndexSet(integer: index))
                            }
                        }) {
                            Image(systemName: "xmark.circle.fill")
                                .font(EditorTheme.fontCaption)
                                .foregroundColor(EditorTheme.textMuted)
                        }
                        .buttonStyle(.plain)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(EditorTheme.surface)
                    .cornerRadius(6)
                }
            }
            
            SettingsGroup(title: "Render Profiles") {
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(Array(viewModel.renderProfiles.enumerated()), id: \.element.id) { index, _ in
                        QualityProfileCard(
                            name: $viewModel.renderProfiles[index].name,
                            quality: $viewModel.renderProfiles[index].quality,
                            defaultProfileName: $viewModel.defaultRenderProfile,
                            canRemove: viewModel.renderProfiles.count > 1
                        ) {
                            viewModel.removeRenderProfile(at: index)
                        } onApply: {
                            viewModel.applySceneQuality(viewModel.renderProfiles[index].quality, overrideProject: true)
                            refreshSceneSettings()
                        } onChange: {
                            viewModel.apply()
                        }
                    }
                    
                    Button("Add Profile") {
                        viewModel.addRenderProfile()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
            }
            
            SettingsGroup(title: "Quality Presets") {
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(Array(viewModel.qualityPresets.enumerated()), id: \.element.id) { index, _ in
                        QualityPresetCard(
                            name: $viewModel.qualityPresets[index].name,
                            quality: $viewModel.qualityPresets[index].quality,
                            canRemove: viewModel.qualityPresets.count > 1
                        ) {
                            viewModel.removeQualityPreset(at: index)
                        } onApply: {
                            viewModel.applySceneQuality(viewModel.qualityPresets[index].quality, overrideProject: true)
                            refreshSceneSettings()
                        } onChange: {
                            viewModel.apply()
                        }
                    }
                    
                    Button("Add Preset") {
                        viewModel.addQualityPreset()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
            }
            
            SettingsGroup(title: "Input Bindings") {
                InputBindingsEditor(
                    bindings: $viewModel.inputBindings,
                    onAdd: viewModel.addInputBinding,
                    onRemove: viewModel.removeInputBinding(at:),
                    onChange: viewModel.apply
                )
            }
        }
    }
}

private struct SceneSettingsPanel: View {
    @ObservedObject var viewModel: SceneSettingsViewModel
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            SettingsGroup(title: "Environment") {
                SettingsRow(title: "Skybox") {
                    HStack(spacing: 6) {
                        Text(viewModel.skyboxPath == "Builtin Sky" ? "Builtin Sky" : URL(fileURLWithPath: viewModel.skyboxPath).lastPathComponent)
                            .font(EditorTheme.fontBody)
                            .foregroundColor(EditorTheme.textPrimary)
                            .lineLimit(1)
                        Spacer()
                        Button("Load") {
                            viewModel.loadSkybox()
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        Button("Reset") {
                            viewModel.resetSkybox()
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                    }
                }

                SettingsRow(title: "Sky Mode") {
                    Picker("", selection: $viewModel.skyMode) {
                        Text("Procedural").tag(0)
                        Text("HDRI").tag(1)
                    }
                    .pickerStyle(.segmented)
                    .frame(width: 220)
                    .onChange(of: viewModel.skyMode) { _ in viewModel.apply() }
                }
                
                Toggle("Show Skybox", isOn: $viewModel.skyboxVisible)
                    .onChange(of: viewModel.skyboxVisible) { _ in viewModel.apply() }

                Toggle("Auto Sun Color", isOn: $viewModel.autoSunColor)
                    .onChange(of: viewModel.autoSunColor) { _ in viewModel.apply() }
                
                SettingsSlider(title: "Exposure (EV)", value: $viewModel.exposureEV, range: -8...8) {
                    viewModel.apply()
                }
                SettingsSlider(title: "IBL Intensity", value: $viewModel.iblIntensity, range: 0...4) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Sky Brightness", value: $viewModel.skyIntensity, range: 0...4) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Saturation", value: $viewModel.saturation, range: 0...3) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Contrast", value: $viewModel.contrast, range: 0.2...3) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Background Blur (LOD)", value: $viewModel.blurLevel, range: 0...8) {
                    viewModel.apply()
                }
                
                HStack(spacing: 8) {
                    ColorPicker("Tint", selection: $viewModel.tint)
                        .onChange(of: viewModel.tint) { _ in viewModel.apply() }
                    Spacer()
                }
                
                HStack(spacing: 8) {
                    Text("Rotation")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(EditorTheme.textMuted)
                    Vector3Field(values: $viewModel.rotation) {
                        viewModel.apply()
                    }
                }
            }
            
            SettingsGroup(title: "Fog") {
                Toggle("Enable Fog", isOn: $viewModel.fogEnabled)
                    .onChange(of: viewModel.fogEnabled) { _ in viewModel.apply() }
                
                HStack(spacing: 8) {
                    ColorPicker("Color", selection: $viewModel.fogColor)
                        .onChange(of: viewModel.fogColor) { _ in viewModel.apply() }
                    Spacer()
                }
                
                SettingsSlider(title: "Density", value: $viewModel.fogDensity, range: 0...1) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Start Distance", value: $viewModel.fogStart, range: 0...500) {
                    viewModel.apply()
                }
                SettingsSlider(title: "End Distance", value: $viewModel.fogEnd, range: 10...1000) {
                    viewModel.apply()
                }
                
                Toggle("Height Fog", isOn: $viewModel.heightFog)
                    .onChange(of: viewModel.heightFog) { _ in viewModel.apply() }
                
                SettingsSlider(title: "Height", value: $viewModel.fogHeight, range: -200...200) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Falloff", value: $viewModel.fogHeightFalloff, range: 0.01...2) {
                    viewModel.apply()
                }

                SettingsSlider(title: "Scattering", value: $viewModel.fogScattering, range: 0...2) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Anisotropy", value: $viewModel.fogAnisotropy, range: 0...0.9) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Temporal Blend", value: $viewModel.fogHistoryWeight, range: 0...0.98) {
                    viewModel.apply()
                }
                SettingsRow(title: "Volumetric Quality") {
                    Picker("", selection: $viewModel.fogQuality) {
                        Text("Low").tag(0)
                        Text("Medium").tag(1)
                        Text("High").tag(2)
                    }
                    .frame(width: 220)
                    .onChange(of: viewModel.fogQuality) { _ in viewModel.apply() }
                }
            }
            
            SettingsGroup(title: "Post-Process") {
                Toggle("Enabled", isOn: $viewModel.postEnabled)
                    .onChange(of: viewModel.postEnabled) { _ in viewModel.apply() }
                
                Toggle("Bloom", isOn: $viewModel.bloom)
                    .onChange(of: viewModel.bloom) { _ in viewModel.apply() }
                SettingsSlider(title: "Bloom Intensity", value: $viewModel.bloomIntensity, range: 0...5) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Bloom Threshold", value: $viewModel.bloomThreshold, range: 0...5) {
                    viewModel.apply()
                }
                
                Toggle("Tone Mapping", isOn: $viewModel.toneMapping)
                    .onChange(of: viewModel.toneMapping) { _ in viewModel.apply() }
                SettingsRow(title: "Tone Mapping Mode") {
                    Picker("", selection: $viewModel.toneMappingMode) {
                        Text("Filmic").tag(1)
                        Text("ACES").tag(2)
                    }
                    .frame(width: 220)
                    .onChange(of: viewModel.toneMappingMode) { _ in viewModel.apply() }
                }
                Toggle("Color Grading", isOn: $viewModel.colorGrading)
                    .onChange(of: viewModel.colorGrading) { _ in viewModel.apply() }
                SettingsRow(title: "LUT") {
                    HStack(spacing: 8) {
                        Button("Load") {
                            viewModel.loadColorGradingLUT()
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        Button("Clear") {
                            viewModel.resetColorGradingLUT()
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        Text(viewModel.colorGradingLUTPath.isEmpty
                             ? "None"
                             : URL(fileURLWithPath: viewModel.colorGradingLUTPath).lastPathComponent)
                            .font(EditorTheme.fontBody)
                            .foregroundColor(EditorTheme.textMuted)
                    }
                }
                SettingsSlider(title: "LUT Intensity", value: $viewModel.colorGradingIntensity, range: 0...1) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Saturation", value: $viewModel.postSaturation, range: 0...3) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Contrast", value: $viewModel.postContrast, range: 0.2...3) {
                    viewModel.apply()
                }
                
                Toggle("Vignette", isOn: $viewModel.vignette)
                    .onChange(of: viewModel.vignette) { _ in viewModel.apply() }
                SettingsSlider(title: "Vignette Intensity", value: $viewModel.vignetteIntensity, range: 0...1) {
                    viewModel.apply()
                }

                Toggle("Film Grain", isOn: $viewModel.filmGrain)
                    .onChange(of: viewModel.filmGrain) { _ in viewModel.apply() }
                SettingsSlider(title: "Grain Intensity", value: $viewModel.filmGrainIntensity, range: 0...1) {
                    viewModel.apply()
                }
                
                Toggle("SSAO", isOn: $viewModel.ssao)
                    .onChange(of: viewModel.ssao) { _ in viewModel.apply() }
                SettingsSlider(title: "SSAO Radius", value: $viewModel.ssaoRadius, range: 0.1...2) {
                    viewModel.apply()
                }
                SettingsSlider(title: "SSAO Strength", value: $viewModel.ssaoStrength, range: 0...3) {
                    viewModel.apply()
                }
                
                Toggle("SSR", isOn: $viewModel.ssr)
                    .onChange(of: viewModel.ssr) { _ in viewModel.apply() }
                SettingsSlider(title: "SSR Max Roughness", value: $viewModel.ssrMaxRoughness, range: 0...1) {
                    viewModel.apply()
                }
                SettingsSlider(title: "SSR Thickness", value: $viewModel.ssrThickness, range: 0.01...1) {
                    viewModel.apply()
                }
                
                Toggle("TAA", isOn: $viewModel.taa)
                    .onChange(of: viewModel.taa) { _ in viewModel.apply() }
                SettingsSlider(title: "TAA Sharpness", value: $viewModel.taaSharpness, range: 0...1) {
                    viewModel.apply()
                }
                
                Toggle("FXAA", isOn: $viewModel.fxaa)
                    .onChange(of: viewModel.fxaa) { _ in viewModel.apply() }
                
                Toggle("Motion Blur", isOn: $viewModel.motionBlur)
                    .onChange(of: viewModel.motionBlur) { _ in viewModel.apply() }
                SettingsSlider(title: "Motion Blur Strength", value: $viewModel.motionBlurStrength, range: 0...2) {
                    viewModel.apply()
                }
                
                Toggle("Depth of Field", isOn: $viewModel.dof)
                    .onChange(of: viewModel.dof) { _ in viewModel.apply() }
                SettingsSlider(title: "Focus Distance", value: $viewModel.dofFocusDistance, range: 0.1...50) {
                    viewModel.apply()
                }
                SettingsSlider(title: "Aperture", value: $viewModel.dofAperture, range: 1.2...16) {
                    viewModel.apply()
                }
            }
            
            SettingsGroup(title: "Quality Overrides") {
                Toggle("Override Project", isOn: $viewModel.qualityOverride)
                    .onChange(of: viewModel.qualityOverride) { _ in viewModel.apply() }
                
                SettingsRow(title: "Shadow Quality") {
                    Picker("", selection: $viewModel.shadowQuality) {
                        Text("Low").tag(0)
                        Text("Medium").tag(1)
                        Text("High").tag(2)
                        Text("Ultra").tag(3)
                    }
                    .labelsHidden()
                    .frame(width: 140)
                    .onChange(of: viewModel.shadowQuality) { _ in viewModel.apply() }
                }
                
                SettingsRow(title: "Shadow Resolution") {
                    Stepper(value: $viewModel.shadowResolution, in: 256...4096, step: 256) {
                        Text("\(viewModel.shadowResolution)")
                            .font(EditorTheme.fontBody)
                    }
                    .onChange(of: viewModel.shadowResolution) { _ in viewModel.apply() }
                }
                
                SettingsRow(title: "MSAA Samples") {
                    Stepper(value: $viewModel.msaaSamples, in: 1...8, step: 1) {
                        Text("\(viewModel.msaaSamples)x")
                            .font(EditorTheme.fontBody)
                    }
                    .onChange(of: viewModel.msaaSamples) { _ in viewModel.apply() }
                }
                
                SettingsRow(title: "Anisotropy") {
                    Stepper(value: $viewModel.anisotropy, in: 1...16, step: 1) {
                        Text("\(viewModel.anisotropy)x")
                            .font(EditorTheme.fontBody)
                    }
                    .onChange(of: viewModel.anisotropy) { _ in viewModel.apply() }
                }
                
                SettingsSlider(title: "Render Scale", value: $viewModel.renderScale, range: 0.5...2) {
                    viewModel.apply()
                }
                SettingsSlider(title: "LOD Bias", value: $viewModel.lodBias, range: -2...2) {
                    viewModel.apply()
                }
                
                SettingsRow(title: "Texture Quality") {
                    Picker("", selection: $viewModel.textureQuality) {
                        Text("Low").tag(0)
                        Text("Medium").tag(1)
                        Text("High").tag(2)
                        Text("Ultra").tag(3)
                    }
                    .labelsHidden()
                    .frame(width: 140)
                    .onChange(of: viewModel.textureQuality) { _ in viewModel.apply() }
                }
            }
        }
    }
}

private struct QualityProfileCard: View {
    @Binding var name: String
    @Binding var quality: QualitySettings
    @Binding var defaultProfileName: String
    let canRemove: Bool
    let onRemove: () -> Void
    let onApply: () -> Void
    let onChange: () -> Void
    @State private var isExpanded: Bool = false
    @State private var lastName: String
    
    init(name: Binding<String>,
         quality: Binding<QualitySettings>,
         defaultProfileName: Binding<String>,
         canRemove: Bool,
         onRemove: @escaping () -> Void,
         onApply: @escaping () -> Void,
         onChange: @escaping () -> Void) {
        _name = name
        _quality = quality
        _defaultProfileName = defaultProfileName
        self.canRemove = canRemove
        self.onRemove = onRemove
        self.onApply = onApply
        self.onChange = onChange
        _lastName = State(initialValue: name.wrappedValue)
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                TextField("Profile Name", text: $name)
                    .textFieldStyle(.plain)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(EditorTheme.panelBackground)
                    .cornerRadius(6)
                    .onChange(of: name) { newValue in
                        if lastName == defaultProfileName {
                            defaultProfileName = newValue
                        }
                        lastName = newValue
                        onChange()
                    }
                
                if defaultProfileName == name {
                    Text("Default")
                        .font(EditorTheme.fontCaption)
                        .foregroundColor(EditorTheme.textAccent)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(EditorTheme.panelBackground)
                        .cornerRadius(6)
                } else {
                    Button("Set Default") {
                        defaultProfileName = name
                        onChange()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
                
                Button("Apply") {
                    onApply()
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                
                if canRemove {
                    Button(action: onRemove) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                    .foregroundColor(.red.opacity(0.85))
                }
                
                Button {
                    withAnimation(.easeInOut(duration: 0.2)) {
                        isExpanded.toggle()
                    }
                } label: {
                    Image(systemName: "chevron.right")
                        .rotationEffect(.degrees(isExpanded ? 90 : 0))
                        .foregroundColor(EditorTheme.textMuted)
                }
                .buttonStyle(.plain)
            }
            
            if isExpanded {
                Divider()
                    .overlay(EditorTheme.panelStroke)
                QualitySettingsEditor(quality: $quality, onChange: onChange)
                    .padding(.top, 6)
            }
        }
        .padding(10)
        .background(EditorTheme.surfaceElevated)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(8)
    }
}

private struct QualityPresetCard: View {
    @Binding var name: String
    @Binding var quality: QualitySettings
    let canRemove: Bool
    let onRemove: () -> Void
    let onApply: () -> Void
    let onChange: () -> Void
    @State private var isExpanded: Bool = false
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                TextField("Preset Name", text: $name)
                    .textFieldStyle(.plain)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(EditorTheme.panelBackground)
                    .cornerRadius(6)
                    .onChange(of: name) { _ in
                        onChange()
                    }
                
                Button("Apply") {
                    onApply()
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                
                if canRemove {
                    Button(action: onRemove) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                    .foregroundColor(.red.opacity(0.85))
                }
                
                Button {
                    withAnimation(.easeInOut(duration: 0.2)) {
                        isExpanded.toggle()
                    }
                } label: {
                    Image(systemName: "chevron.right")
                        .rotationEffect(.degrees(isExpanded ? 90 : 0))
                        .foregroundColor(EditorTheme.textMuted)
                }
                .buttonStyle(.plain)
            }
            
            if isExpanded {
                Divider()
                    .overlay(EditorTheme.panelStroke)
                QualitySettingsEditor(quality: $quality, onChange: onChange)
                    .padding(.top, 6)
            }
        }
        .padding(10)
        .background(EditorTheme.surfaceElevated)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(8)
    }
}

private struct QualitySettingsEditor: View {
    @Binding var quality: QualitySettings
    let onChange: () -> Void
    
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            SettingsRow(title: "Shadow Quality") {
                Picker("", selection: $quality.shadowQuality) {
                    Text("Low").tag(0)
                    Text("Medium").tag(1)
                    Text("High").tag(2)
                    Text("Ultra").tag(3)
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: quality.shadowQuality) { _ in onChange() }
            }
            
            SettingsRow(title: "Shadow Resolution") {
                Picker("", selection: $quality.shadowResolution) {
                    Text("512").tag(512)
                    Text("1024").tag(1024)
                    Text("2048").tag(2048)
                    Text("4096").tag(4096)
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: quality.shadowResolution) { _ in onChange() }
            }
            
            SettingsRow(title: "MSAA Samples") {
                Picker("", selection: $quality.msaaSamples) {
                    Text("1x").tag(1)
                    Text("2x").tag(2)
                    Text("4x").tag(4)
                    Text("8x").tag(8)
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: quality.msaaSamples) { _ in onChange() }
            }
            
            SettingsRow(title: "Anisotropy") {
                Picker("", selection: $quality.anisotropy) {
                    Text("1x").tag(1)
                    Text("2x").tag(2)
                    Text("4x").tag(4)
                    Text("8x").tag(8)
                    Text("16x").tag(16)
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: quality.anisotropy) { _ in onChange() }
            }
            
            SettingsSlider(title: "Render Scale", value: $quality.renderScale, range: 0.5...2.0) {
                onChange()
            }
            
            SettingsSlider(title: "LOD Bias", value: $quality.lodBias, range: -2.0...2.0) {
                onChange()
            }
            
            SettingsRow(title: "Texture Quality") {
                Picker("", selection: $quality.textureQuality) {
                    Text("Low").tag(0)
                    Text("Medium").tag(1)
                    Text("High").tag(2)
                    Text("Ultra").tag(3)
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: quality.textureQuality) { _ in onChange() }
            }
        }
    }
}

private struct InputBindingsEditor: View {
    @Binding var bindings: [InputBindingItem]
    let onAdd: () -> Void
    let onRemove: (Int) -> Void
    let onChange: () -> Void
    
    private let keyOptions: [String] = [
        "", "W", "A", "S", "D", "Q", "E", "Space", "Shift", "Control", "Alt", "Command", "Up", "Down", "Left", "Right", "Escape"
    ]
    private let mouseOptions: [String] = ["", "Left", "Right", "Middle"]
    
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(Array(bindings.enumerated()), id: \.element.id) { index, _ in
                InputBindingRow(
                    binding: $bindings[index],
                    keyOptions: keyOptions,
                    mouseOptions: mouseOptions,
                    onRemove: { onRemove(index) },
                    onChange: onChange
                )
            }
            
            Button("Add Binding") {
                onAdd()
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
        }
    }
}

private struct InputBindingRow: View {
    @Binding var binding: InputBindingItem
    let keyOptions: [String]
    let mouseOptions: [String]
    let onRemove: () -> Void
    let onChange: () -> Void
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                TextField("Action", text: $binding.action)
                    .textFieldStyle(.plain)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(EditorTheme.panelBackground)
                    .cornerRadius(6)
                    .onChange(of: binding.action) { _ in onChange() }
                
                Button(action: onRemove) {
                    Image(systemName: "trash")
                }
                .buttonStyle(.borderless)
                .foregroundColor(.red.opacity(0.85))
            }
            
            SettingsRow(title: "Key") {
                Picker("", selection: $binding.key) {
                    ForEach(keyOptions, id: \.self) { key in
                        Text(key.isEmpty ? "None" : key).tag(key)
                    }
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: binding.key) { _ in onChange() }
            }
            
            SettingsRow(title: "Mouse") {
                Picker("", selection: $binding.mouseButton) {
                    ForEach(mouseOptions, id: \.self) { button in
                        Text(button.isEmpty ? "None" : button).tag(button)
                    }
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: binding.mouseButton) { _ in onChange() }
            }
            
            HStack(spacing: 10) {
                Toggle("Invert", isOn: $binding.invert)
                    .onChange(of: binding.invert) { _ in onChange() }
                Spacer()
                Slider(value: $binding.scale, in: 0.1...2.0, step: 0.05)
                    .frame(width: 160)
                    .onChange(of: binding.scale) { _ in onChange() }
                Text(String(format: "%.2f", binding.scale))
                    .font(EditorTheme.fontBody)
                    .foregroundColor(EditorTheme.textPrimary)
                    .frame(width: 48, alignment: .trailing)
            }
        }
        .padding(10)
        .background(EditorTheme.surfaceElevated)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(8)
    }
}

private struct SettingsGroup<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content
    @State private var isExpanded: Bool = true
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Button {
                withAnimation(.easeInOut(duration: 0.2)) {
                    isExpanded.toggle()
                }
            } label: {
                HStack(spacing: 8) {
                    Text(title)
                        .font(EditorTheme.font(size: 12, weight: .semibold))
                        .foregroundColor(EditorTheme.textPrimary)
                    Spacer()
                    Image(systemName: "chevron.right")
                        .font(EditorTheme.font(size: 10, weight: .semibold))
                        .rotationEffect(.degrees(isExpanded ? 90 : 0))
                        .foregroundColor(EditorTheme.textMuted)
                }
                .padding(.vertical, 6)
            }
            .buttonStyle(.plain)
            
            if isExpanded {
                Divider()
                    .overlay(EditorTheme.panelStroke)
                VStack(alignment: .leading, spacing: 10) {
                    content
                }
                .padding(.top, 10)
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(8)
    }
}

private struct SettingsRow<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content
    
    var body: some View {
        HStack(spacing: 10) {
            Text(title)
                .font(EditorTheme.fontBody)
                .foregroundColor(EditorTheme.textMuted)
                .frame(width: 150, alignment: .leading)
            content
        }
    }
}

private struct SettingsSlider: View {
    let title: String
    @Binding var value: Double
    var range: ClosedRange<Double>
    var onCommit: () -> Void
    
    var body: some View {
        HStack(spacing: 10) {
            Text(title)
                .font(EditorTheme.fontBody)
                .foregroundColor(EditorTheme.textMuted)
                .frame(width: 150, alignment: .leading)
            Slider(value: $value, in: range, onEditingChanged: { editing in
                if !editing {
                    onCommit()
                }
            })
            Text(String(format: "%.2f", value))
                .font(EditorTheme.fontBody)
                .foregroundColor(EditorTheme.textPrimary)
                .frame(width: 50, alignment: .trailing)
        }
    }
}

private struct Vector3Field: View {
    @Binding var values: [Double]
    let onCommit: () -> Void
    
    var body: some View {
        HStack(spacing: 6) {
            ForEach(0..<3, id: \.self) { index in
                TextField("", value: Binding(
                    get: { values[index] },
                    set: { newValue in
                        values[index] = newValue
                    }
                ), format: .number.precision(.fractionLength(2)))
                .textFieldStyle(.plain)
                .frame(width: 52)
                .padding(.horizontal, 6)
                .padding(.vertical, 4)
                .background(EditorTheme.surface)
                .cornerRadius(6)
                .onSubmit {
                    onCommit()
                }
            }
        }
    }
}
