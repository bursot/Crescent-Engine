import SwiftUI

#if PLAYER_APP
@main
struct CrescentPlayerApp: App {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var runtimeState = GameRuntimeState()

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
