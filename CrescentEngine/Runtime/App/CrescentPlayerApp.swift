import Foundation
import SwiftUI
import Darwin

private enum PlayerCommandLine {
    static func runIfRequested() -> Int32? {
        let arguments = CommandLine.arguments
        guard arguments.count > 1 else {
            return nil
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
