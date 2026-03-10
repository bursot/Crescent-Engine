import Foundation
import Combine
import Darwin

@MainActor
final class GameRuntimeState: ObservableObject {
    @Published private(set) var isLoading: Bool = true
    @Published private(set) var isRunning: Bool = false
    @Published private(set) var errorMessage: String?
    @Published private(set) var gameTitle: String = "Crescent Game"

    private var didBootstrap = false

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

        bridge.setViewMode(1)
        bridge.enterPlayMode()
        isRunning = true
        isLoading = false
    }

    func shutdown() {
        let bridge = AppBridge.shared()
        if isRunning {
            bridge.exitPlayMode()
            isRunning = false
        }
        bridge.shutdown()
    }

    private func fail(_ message: String) {
        errorMessage = message
        isLoading = false
        isRunning = false
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
