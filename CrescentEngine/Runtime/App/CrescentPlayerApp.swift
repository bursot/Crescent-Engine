import Foundation
import SwiftUI
import Darwin

private enum PlayerCommandLine {
    static func runIfRequested() -> Int32? {
        let arguments = CommandLine.arguments
        guard arguments.count > 1 else {
            return nil
        }

        if arguments[1] == "--cook-environment" {
            guard arguments.count >= 5 else {
                fputs("Usage: CrescentPlayer --cook-environment <Project.cproj> <SourceHDRI> <Output.cenv>\n", stderr)
                return 2
            }

            let projectPath = arguments[2]
            let sourcePath = arguments[3]
            let outputPath = arguments[4]
            let bridge = RuntimeBridge.shared()

            guard bridge.initialize() else {
                fputs("Failed to initialize runtime bridge for environment cooking.\n", stderr)
                return 1
            }
            defer {
                bridge.shutdown()
            }

            guard bridge.openProject(path: projectPath) else {
                fputs("Failed to open project for environment cooking.\n", stderr)
                return 1
            }

            guard bridge.cookEnvironmentMap(path: sourcePath, outputPath: outputPath) else {
                fputs("Failed to cook environment map.\n", stderr)
                return 1
            }

            fputs("Cooked environment: \(outputPath)\n", stdout)
            return 0
        }

        if arguments[1] == "--cook-static-lightmap" {
            guard arguments.count >= 5 else {
                fputs("Usage: CrescentPlayer --cook-static-lightmap <Project.cproj> <SourceLightmap.exr> <OutputLightmap.ktx2>\n", stderr)
                return 2
            }

            let projectPath = arguments[2]
            let sourcePath = arguments[3]
            let outputPath = arguments[4]
            let bridge = RuntimeBridge.shared()

            guard bridge.initialize() else {
                fputs("Failed to initialize runtime bridge for static lightmap cooking.\n", stderr)
                return 1
            }
            defer {
                bridge.shutdown()
            }

            guard bridge.openProject(path: projectPath) else {
                fputs("Failed to open project for static lightmap cooking.\n", stderr)
                return 1
            }

            guard bridge.cookStaticLightmap(path: sourcePath, outputPath: outputPath) else {
                fputs("Failed to cook static lightmap.\n", stderr)
                return 1
            }

            fputs("Cooked static lightmap: \(outputPath)\n", stdout)
            return 0
        }

        if arguments[1] == "--bake-scene-lighting" {
            guard arguments.count >= 5 else {
                fputs("Usage: CrescentPlayer --bake-scene-lighting <Project.cproj> <SourceScene.cscene> <OutputScene.cscene>\n", stderr)
                return 2
            }

            let projectPath = arguments[2]
            let sourceScenePath = arguments[3]
            let outputScenePath = arguments[4]
            let bridge = RuntimeBridge.shared()

            guard bridge.initialize() else {
                fputs("Failed to initialize runtime bridge for baked lighting.\n", stderr)
                return 1
            }
            defer {
                bridge.shutdown()
            }

            guard bridge.openProject(path: projectPath) else {
                fputs("Failed to open project for baked lighting.\n", stderr)
                return 1
            }

            guard bridge.loadScene(path: sourceScenePath) else {
                fputs("Failed to load source scene for baked lighting.\n", stderr)
                return 1
            }

            let stats = bridge.bakeSceneStaticLighting()
            if let atlasCount = stats["atlasCount"] as? NSNumber,
               let lightCount = stats["bakedLightCount"] as? NSNumber,
               let texelCount = stats["bakedTexelCount"] as? NSNumber {
                fputs("Baked \(atlasCount.intValue) lightmap atlases from \(lightCount.intValue) lights (\(texelCount.intValue) texels).\n", stdout)
            }

            guard bridge.saveScene(path: outputScenePath) else {
                fputs("Failed to save baked source scene.\n", stderr)
                return 1
            }

            return 0
        }

        guard arguments[1] == "--cook-scene" else {
            return nil
        }

        guard arguments.count >= 5 else {
            fputs("Usage: CrescentPlayer --cook-scene <Project.cproj> <SourceScene.cscene> <OutputScene.ccscene>\n", stderr)
            return 2
        }

        let projectPath = arguments[2]
        let sourceScenePath = arguments[3]
        let outputScenePath = arguments[4]
        let bridge = RuntimeBridge.shared()

        guard bridge.initialize() else {
            fputs("Failed to initialize runtime bridge for scene cooking.\n", stderr)
            return 1
        }
        defer {
            bridge.shutdown()
        }

        guard bridge.openProject(path: projectPath) else {
            fputs("Failed to open project for scene cooking.\n", stderr)
            return 1
        }

        guard bridge.loadScene(path: sourceScenePath) else {
            fputs("Failed to load source scene for scene cooking.\n", stderr)
            return 1
        }

        guard bridge.saveCookedRuntimeScene(path: outputScenePath, includeEditorOnly: false) else {
            fputs("Failed to write cooked runtime scene.\n", stderr)
            return 1
        }

        return 0
    }
}

#if PLAYER_APP
@main
struct CrescentPlayerApp: App {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var runtimeState = GameRuntimeState()

    init() {
        if let status = PlayerCommandLine.runIfRequested() {
            Darwin.exit(status)
        }
    }

    var body: some Scene {
        WindowGroup {
            GameRuntimeView(runtimeState: runtimeState)
        }
        .onChange(of: scenePhase) { _, newPhase in
            if newPhase == .background {
                runtimeState.shutdown()
            }
        }
        .windowStyle(.hiddenTitleBar)
        .windowToolbarStyle(.unified)
        .defaultSize(width: 1280, height: 720)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
#endif
