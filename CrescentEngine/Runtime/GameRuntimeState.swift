import Foundation
import Combine
import Darwin

enum RuntimeAntiAliasingMode: String, Codable, CaseIterable, Identifiable {
    case off = "Off"
    case fxaa = "FXAA"
    case taa = "TAA"

    var id: String { rawValue }
}

enum RuntimeUpscalerMode: String, Codable, CaseIterable, Identifiable {
    case off = "Off"
    case metalFX = "MetalFX"

    var id: String { rawValue }
}

enum RuntimeGraphicsPreset: String, CaseIterable, Identifiable {
    case low = "Low"
    case medium = "Medium"
    case high = "High"
    case ultra = "Ultra"

    var id: String { rawValue }

    func apply(to settings: inout RuntimeGraphicsSettings) {
        switch self {
        case .low:
            settings.upscaler = .off
            settings.renderScale = 0.75
            settings.shadowQuality = 0
            settings.shadowResolution = 512
            settings.msaaSamples = 1
            settings.anisotropy = 2
            settings.textureQuality = 0
            settings.lodBias = 1.0
            settings.antiAliasing = .fxaa
            settings.ssao = false
            settings.ssr = false
            settings.motionBlur = false
            settings.depthOfField = false
            settings.vignette = false
            settings.filmGrain = false
            settings.bloom = false
        case .medium:
            settings.upscaler = .off
            settings.renderScale = 0.9
            settings.shadowQuality = 1
            settings.shadowResolution = 1024
            settings.msaaSamples = 2
            settings.anisotropy = 4
            settings.textureQuality = 1
            settings.lodBias = 0.5
            settings.antiAliasing = .fxaa
            settings.ssao = true
            settings.ssr = false
            settings.motionBlur = false
            settings.depthOfField = false
            settings.vignette = true
            settings.filmGrain = false
            settings.bloom = true
        case .high:
            settings.upscaler = .off
            settings.renderScale = 1.0
            settings.shadowQuality = 2
            settings.shadowResolution = 2048
            settings.msaaSamples = 4
            settings.anisotropy = 8
            settings.textureQuality = 2
            settings.lodBias = 0.0
            settings.antiAliasing = .taa
            settings.ssao = true
            settings.ssr = true
            settings.motionBlur = false
            settings.depthOfField = false
            settings.vignette = true
            settings.filmGrain = false
            settings.bloom = true
        case .ultra:
            settings.upscaler = .off
            settings.renderScale = 1.0
            settings.shadowQuality = 3
            settings.shadowResolution = 4096
            settings.msaaSamples = 8
            settings.anisotropy = 16
            settings.textureQuality = 3
            settings.lodBias = -0.5
            settings.antiAliasing = .taa
            settings.ssao = true
            settings.ssr = true
            settings.motionBlur = true
            settings.depthOfField = true
            settings.vignette = true
            settings.filmGrain = true
            settings.bloom = true
        }
    }
}

struct RuntimeGraphicsSettings: Codable, Equatable {
    var postProcessingEnabled: Bool = true
    var renderScale: Double = 1.0
    var upscaler: RuntimeUpscalerMode = .off
    var shadowQuality: Int = 2
    var shadowResolution: Int = 2048
    var msaaSamples: Int = 4
    var anisotropy: Int = 8
    var textureQuality: Int = 2
    var lodBias: Double = 0.0
    var antiAliasing: RuntimeAntiAliasingMode = .taa
    var bloom: Bool = true
    var bloomIntensity: Double = 0.8
    var bloomThreshold: Double = 1.0
    var ssao: Bool = true
    var ssaoRadius: Double = 0.5
    var ssaoStrength: Double = 1.0
    var ssr: Bool = true
    var ssrMaxRoughness: Double = 0.6
    var ssrThickness: Double = 0.1
    var motionBlur: Bool = false
    var motionBlurStrength: Double = 0.5
    var depthOfField: Bool = false
    var dofFocusDistance: Double = 5.0
    var dofAperture: Double = 2.8
    var vignette: Bool = false
    var vignetteIntensity: Double = 0.3
    var filmGrain: Bool = false
    var filmGrainIntensity: Double = 0.15

    init() {}

    init(sceneSettings: [String: Any]) {
        if let post = sceneSettings["postProcess"] as? [String: Any] {
            postProcessingEnabled = post["enabled"] as? Bool ?? postProcessingEnabled
            bloom = post["bloom"] as? Bool ?? bloom
            bloomIntensity = post["bloomIntensity"] as? Double ?? bloomIntensity
            bloomThreshold = post["bloomThreshold"] as? Double ?? bloomThreshold
            ssao = post["ssao"] as? Bool ?? ssao
            ssaoRadius = post["ssaoRadius"] as? Double ?? ssaoRadius
            ssaoStrength = post["ssaoStrength"] as? Double ?? ssaoStrength
            ssr = post["ssr"] as? Bool ?? ssr
            ssrMaxRoughness = post["ssrMaxRoughness"] as? Double ?? ssrMaxRoughness
            ssrThickness = post["ssrThickness"] as? Double ?? ssrThickness
            motionBlur = post["motionBlur"] as? Bool ?? motionBlur
            motionBlurStrength = post["motionBlurStrength"] as? Double ?? motionBlurStrength
            depthOfField = post["depthOfField"] as? Bool ?? depthOfField
            dofFocusDistance = post["dofFocusDistance"] as? Double ?? dofFocusDistance
            dofAperture = post["dofAperture"] as? Double ?? dofAperture
            vignette = post["vignette"] as? Bool ?? vignette
            vignetteIntensity = post["vignetteIntensity"] as? Double ?? vignetteIntensity
            filmGrain = post["filmGrain"] as? Bool ?? filmGrain
            filmGrainIntensity = post["filmGrainIntensity"] as? Double ?? filmGrainIntensity

            let taa = post["taa"] as? Bool ?? false
            let fxaa = post["fxaa"] as? Bool ?? false
            antiAliasing = taa ? .taa : (fxaa ? .fxaa : .off)
        }

        if let quality = sceneSettings["quality"] as? [String: Any] {
            shadowQuality = quality["shadowQuality"] as? Int ?? shadowQuality
            shadowResolution = quality["shadowResolution"] as? Int ?? shadowResolution
            msaaSamples = quality["msaaSamples"] as? Int ?? msaaSamples
            anisotropy = quality["anisotropy"] as? Int ?? anisotropy
            renderScale = quality["renderScale"] as? Double ?? renderScale
            lodBias = quality["lodBias"] as? Double ?? lodBias
            textureQuality = quality["textureQuality"] as? Int ?? textureQuality
            if let upscalerValue = quality["upscaler"] as? Int, upscalerValue == 1 {
                upscaler = .metalFX
            } else {
                upscaler = .off
            }
        }

        clamp()
    }

    mutating func clamp() {
        renderScale = min(max(renderScale, 0.5), 1.5)
        shadowQuality = min(max(shadowQuality, 0), 3)
        shadowResolution = [512, 1024, 2048, 4096].contains(shadowResolution) ? shadowResolution : 2048
        msaaSamples = [1, 2, 4, 8].contains(msaaSamples) ? msaaSamples : 4
        anisotropy = [1, 2, 4, 8, 16].contains(anisotropy) ? anisotropy : 8
        textureQuality = min(max(textureQuality, 0), 3)
        lodBias = min(max(lodBias, -1.0), 2.0)
        bloomIntensity = min(max(bloomIntensity, 0.0), 2.0)
        bloomThreshold = min(max(bloomThreshold, 0.0), 4.0)
        ssaoRadius = min(max(ssaoRadius, 0.1), 3.0)
        ssaoStrength = min(max(ssaoStrength, 0.0), 3.0)
        ssrMaxRoughness = min(max(ssrMaxRoughness, 0.05), 1.0)
        ssrThickness = min(max(ssrThickness, 0.01), 1.0)
        motionBlurStrength = min(max(motionBlurStrength, 0.0), 1.0)
        dofFocusDistance = min(max(dofFocusDistance, 0.1), 100.0)
        dofAperture = min(max(dofAperture, 1.2), 16.0)
        vignetteIntensity = min(max(vignetteIntensity, 0.0), 1.0)
        filmGrainIntensity = min(max(filmGrainIntensity, 0.0), 1.0)
    }

    func toSceneSettingsPayload() -> [String: Any] {
        [
            "postProcess": [
                "enabled": postProcessingEnabled,
                "bloom": bloom,
                "bloomIntensity": bloomIntensity,
                "bloomThreshold": bloomThreshold,
                "ssao": ssao,
                "ssaoRadius": ssaoRadius,
                "ssaoStrength": ssaoStrength,
                "ssr": ssr,
                "ssrMaxRoughness": ssrMaxRoughness,
                "ssrThickness": ssrThickness,
                "taa": antiAliasing == .taa,
                "fxaa": antiAliasing == .fxaa,
                "motionBlur": motionBlur,
                "motionBlurStrength": motionBlurStrength,
                "depthOfField": depthOfField,
                "dofFocusDistance": dofFocusDistance,
                "dofAperture": dofAperture,
                "vignette": vignette,
                "vignetteIntensity": vignetteIntensity,
                "filmGrain": filmGrain,
                "filmGrainIntensity": filmGrainIntensity
            ],
            "quality": [
                "overrideProject": true,
                "shadowQuality": shadowQuality,
                "shadowResolution": shadowResolution,
                "msaaSamples": msaaSamples,
                "anisotropy": anisotropy,
                "renderScale": renderScale,
                "lodBias": lodBias,
                "textureQuality": textureQuality,
                "upscaler": upscaler == .metalFX ? 1 : 0
            ]
        ]
    }
}

@MainActor
final class GameRuntimeState: ObservableObject {
    @Published private(set) var isLoading: Bool = true
    @Published private(set) var isRunning: Bool = false
    @Published private(set) var errorMessage: String?
    @Published private(set) var gameTitle: String = "Crescent Game"
    @Published var isSettingsMenuPresented: Bool = false
    @Published var graphicsSettings = RuntimeGraphicsSettings()
    @Published private(set) var selectedGraphicsPreset: RuntimeGraphicsPreset?

    private var didBootstrap = false
    private var sceneDefaultGraphicsSettings = RuntimeGraphicsSettings()
    private var graphicsSettingsStorageKey = "crescent.runtime.graphics.default"
    private var pendingGraphicsApplyWorkItem: DispatchWorkItem?

    func bootstrapIfNeeded() {
        guard !didBootstrap else {
            return
        }
        didBootstrap = true

        let bridge = AppBridge.shared()
        guard let resourceURL = Bundle.main.resourceURL else {
            fail("Bundle resources are unavailable.")
            return
        }

        let gameDataURL = resourceURL.appendingPathComponent("GameData", isDirectory: true)
        let buildManifestURL = gameDataURL.appendingPathComponent("BuildManifest.json")
        if FileManager.default.fileExists(atPath: buildManifestURL.path) {
            setenv("CRESCENT_REQUIRE_COOKED_TEXTURES", "1", 1)
            setenv("CRESCENT_PREFER_COOKED_SCENES", "1", 1)
            setenv("CRESCENT_REQUIRE_COOKED_SCENES", "1", 1)
        }
        let bundledProjectURL = gameDataURL.appendingPathComponent("Project.cproj")
        guard FileManager.default.fileExists(atPath: bundledProjectURL.path) else {
            fail("Bundled project is missing.")
            return
        }

        guard bridge.openProject(path: bundledProjectURL.path) else {
            fail("Bundled project could not be opened.")
            return
        }

        let settings = bridge.getProjectSettings() as? [String: Any] ?? [:]
        if let productName = settings["productName"] as? String, !productName.isEmpty {
            gameTitle = productName
        }

        let projectInfo = bridge.getProjectInfo() as? [String: Any] ?? [:]
        let projectRootPath = (projectInfo["rootPath"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? gameDataURL.path
        let startupScene = (settings["startupScene"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard !startupScene.isEmpty else {
            fail("Startup scene is not configured.")
            return
        }

        let sceneURL = URL(fileURLWithPath: projectRootPath).appendingPathComponent(startupScene)
        guard FileManager.default.fileExists(atPath: sceneURL.path) else {
            fail("Startup scene is missing: \(startupScene)")
            return
        }

        guard sceneHasRuntimeCamera(at: sceneURL) else {
            fail("Startup scene has no gameplay camera. Add a non-editor Camera entity.")
            return
        }

        guard bridge.loadScene(path: sceneURL.path) else {
            fail("Startup scene could not be loaded.")
            return
        }

        let sceneSettings = bridge.getSceneSettings() as? [String: Any] ?? [:]
        sceneDefaultGraphicsSettings = RuntimeGraphicsSettings(sceneSettings: sceneSettings)
        graphicsSettingsStorageKey = makeGraphicsSettingsStorageKey(startupScene: startupScene, projectRootPath: projectRootPath)
        graphicsSettings = loadStoredGraphicsSettings() ?? sceneDefaultGraphicsSettings
        graphicsSettings.clamp()
        selectedGraphicsPreset = inferPreset(for: graphicsSettings)
        commitGraphicsSettings(graphicsSettings, persist: false)

        bridge.setViewMode(1)
        bridge.enterPlayMode()
        isRunning = true
        isLoading = false
    }

    func shutdown() {
        pendingGraphicsApplyWorkItem?.cancel()
        pendingGraphicsApplyWorkItem = nil
        let bridge = AppBridge.shared()
        if isRunning {
            bridge.exitPlayMode()
            isRunning = false
        }
        bridge.shutdown()
    }

    func toggleSettingsMenu() {
        guard isRunning, errorMessage == nil else {
            return
        }
        isSettingsMenuPresented.toggle()
    }

    func closeSettingsMenu() {
        isSettingsMenuPresented = false
    }

    func updateGraphicsSettings(_ mutate: (inout RuntimeGraphicsSettings) -> Void) {
        guard !isLoading, errorMessage == nil else {
            return
        }
        var updated = graphicsSettings
        mutate(&updated)
        updated.clamp()
        graphicsSettings = updated
        selectedGraphicsPreset = inferPreset(for: updated)
        scheduleGraphicsApply(updated)
    }

    func applyGraphicsPreset(_ preset: RuntimeGraphicsPreset) {
        updateGraphicsSettings { settings in
            preset.apply(to: &settings)
        }
        selectedGraphicsPreset = preset
    }

    func resetGraphicsSettings() {
        pendingGraphicsApplyWorkItem?.cancel()
        pendingGraphicsApplyWorkItem = nil
        graphicsSettings = sceneDefaultGraphicsSettings
        selectedGraphicsPreset = inferPreset(for: graphicsSettings)
        commitGraphicsSettings(graphicsSettings, persist: true)
    }

    private func fail(_ message: String) {
        errorMessage = message
        isLoading = false
        isRunning = false
    }

    private func scheduleGraphicsApply(_ settings: RuntimeGraphicsSettings) {
        pendingGraphicsApplyWorkItem?.cancel()
        let item = DispatchWorkItem { [weak self] in
            Task { @MainActor [weak self] in
                self?.commitGraphicsSettings(settings, persist: true)
            }
        }
        pendingGraphicsApplyWorkItem = item
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.08, execute: item)
    }

    private func commitGraphicsSettings(_ settings: RuntimeGraphicsSettings, persist: Bool) {
        pendingGraphicsApplyWorkItem?.cancel()
        pendingGraphicsApplyWorkItem = nil
        AppBridge.shared().setSceneSettings(settings: settings.toSceneSettingsPayload())
        if persist {
            persistGraphicsSettings(settings)
        }
    }

    private func loadStoredGraphicsSettings() -> RuntimeGraphicsSettings? {
        guard let data = UserDefaults.standard.data(forKey: graphicsSettingsStorageKey) else {
            return nil
        }
        return try? JSONDecoder().decode(RuntimeGraphicsSettings.self, from: data)
    }

    private func persistGraphicsSettings(_ settings: RuntimeGraphicsSettings) {
        guard let data = try? JSONEncoder().encode(settings) else {
            return
        }
        UserDefaults.standard.set(data, forKey: graphicsSettingsStorageKey)
    }

    private func makeGraphicsSettingsStorageKey(startupScene: String, projectRootPath: String) -> String {
        let bundle = Bundle.main.bundleIdentifier ?? "crescent.player"
        let sanitizedRoot = projectRootPath
            .replacingOccurrences(of: ":", with: "_")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: " ", with: "_")
        let sanitizedScene = startupScene
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: " ", with: "_")
        return "crescent.runtime.graphics.\(bundle).\(sanitizedRoot).\(sanitizedScene)"
    }

    private func inferPreset(for settings: RuntimeGraphicsSettings) -> RuntimeGraphicsPreset? {
        for preset in RuntimeGraphicsPreset.allCases {
            var probe = sceneDefaultGraphicsSettings
            preset.apply(to: &probe)
            probe.clamp()
            if probe.renderScale == settings.renderScale &&
                probe.upscaler == settings.upscaler &&
                probe.shadowQuality == settings.shadowQuality &&
                probe.shadowResolution == settings.shadowResolution &&
                probe.msaaSamples == settings.msaaSamples &&
                probe.anisotropy == settings.anisotropy &&
                probe.textureQuality == settings.textureQuality &&
                probe.lodBias == settings.lodBias &&
                probe.antiAliasing == settings.antiAliasing &&
                probe.bloom == settings.bloom &&
                probe.ssao == settings.ssao &&
                probe.ssr == settings.ssr &&
                probe.motionBlur == settings.motionBlur &&
                probe.depthOfField == settings.depthOfField &&
                probe.vignette == settings.vignette &&
                probe.filmGrain == settings.filmGrain {
                return preset
            }
        }
        return nil
    }

    private func sceneHasRuntimeCamera(at url: URL) -> Bool {
        // Cooked runtime scenes are binary; build-time validation already guarantees a gameplay camera.
        if url.pathExtension.lowercased() == "ccscene" {
            return true
        }

        guard let data = try? Data(contentsOf: url),
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let entities = root["entities"] as? [[String: Any]] else {
            return false
        }

        for entity in entities {
            if (entity["editorOnly"] as? Bool) == true {
                continue
            }
            if let active = entity["active"] as? Bool, !active {
                continue
            }
            guard let components = entity["components"] as? [String: Any],
                  let camera = components["Camera"] as? [String: Any] else {
                continue
            }
            if (camera["editorCamera"] as? Bool) == true {
                continue
            }
            return true
        }

        return false
    }
}
