import SwiftUI

@main
struct CrescentEngineApp: App {
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.openWindow) private var openWindow
    @StateObject private var editorState = EditorState()
    
    var body: some Scene {
        WindowGroup {
            ContentView(editorState: editorState)
        }
        .onChange(of: scenePhase) { oldPhase, newPhase in
            if newPhase == .background {
                // Shutdown engine when app goes to background
                CrescentEngineBridge.shared().shutdown()
            }
        }
        .windowStyle(.hiddenTitleBar)
        .windowToolbarStyle(.unified)
        .defaultSize(width: 1280, height: 800)
        
        WindowGroup("Settings", id: "settings") {
            SettingsWindow(editorState: editorState)
        }
        .defaultSize(width: 620, height: 720)

        WindowGroup("Skeleton", id: "skeleton") {
            SkeletonWindow(editorState: editorState)
        }
        .defaultSize(width: 820, height: 640)

        WindowGroup("Animation Sequence", id: "animSequence") {
            AnimationSequenceWindow(editorState: editorState)
        }
        .defaultSize(width: 900, height: 660)

        WindowGroup("Anim Graph", id: "animGraph") {
            AnimGraphWindow(editorState: editorState)
        }
        .defaultSize(width: 980, height: 740)
        .commands {
            // File menu
            CommandGroup(replacing: .newItem) {
                Button("New Scene") {
                    // TODO: Create new scene
                }
                .keyboardShortcut("n", modifiers: [.command])
                
                Button("Open Scene...") {
                    // TODO: Open scene
                }
                .keyboardShortcut("o", modifiers: [.command])
                
                Divider()
                
                Button("Save Scene") {
                    // TODO: Save scene
                }
                .keyboardShortcut("s", modifiers: [.command])
                
                Button("Save Scene As...") {
                    // TODO: Save scene as
                }
                .keyboardShortcut("s", modifiers: [.command, .shift])
            }
            
            // Edit menu additions
            CommandGroup(after: .pasteboard) {
                Divider()
                
                Button("Duplicate") {
                    // TODO: Duplicate selected object
                }
                .keyboardShortcut("d", modifiers: [.command])
                
                Button("Delete") {
                    // TODO: Delete selected object
                }
                .keyboardShortcut(.delete, modifiers: [])
            }
            
            // GameObject menu
            CommandMenu("GameObject") {
                Menu("3D Object") {
                    Button("Cube") {
                        CrescentEngineBridge.shared().createCube()
                    }
                    Button("Sphere") {
                        CrescentEngineBridge.shared().createSphere()
                    }
                    Button("Plane") {
                        CrescentEngineBridge.shared().createPlane()
                    }
                    Button("Cylinder") {
                        CrescentEngineBridge.shared().createCylinder()
                    }
                    Button("Cone") {
                        CrescentEngineBridge.shared().createCone()
                    }
                    Button("Torus") {
                        CrescentEngineBridge.shared().createTorus()
                    }
                    Button("Capsule") {
                        CrescentEngineBridge.shared().createCapsule()
                    }
                }
                
                Menu("Light") {
                    Button("Directional Light") {
                        CrescentEngineBridge.shared().createDirectionalLight()
                    }
                    Button("Point Light") {
                        CrescentEngineBridge.shared().createPointLight()
                    }
                    Button("Spot Light") {
                        CrescentEngineBridge.shared().createSpotLight()
                    }
                }
                
                Divider()
                
                Button("Camera") {
                    CrescentEngineBridge.shared().createCamera()
                }
                
                Divider()
                
                Button("Empty GameObject") {
                    CrescentEngineBridge.shared().createEmpty()
                }
            }

            // Window menu additions
            CommandGroup(after: .windowArrangement) {
                Divider()

                Button("Settings") {
                    editorState.settingsWindowRequested = true
                    openWindow(id: "settings")
                }

                Button("Skeleton") {
                    openWindow(id: "skeleton")
                }

                Button("Animation Sequence") {
                    openWindow(id: "animSequence")
                }

                Button("Anim Graph") {
                    openWindow(id: "animGraph")
                }
                
                Button("Scene View") {
                    // TODO: Focus scene view
                }
                .keyboardShortcut("1", modifiers: [.command])
                
                Button("Hierarchy") {
                    // TODO: Toggle hierarchy
                }
                .keyboardShortcut("2", modifiers: [.command])
                
                Button("Inspector") {
                    // TODO: Toggle inspector
                }
                .keyboardShortcut("3", modifiers: [.command])
                
                Button("Console") {
                    // TODO: Toggle console
                }
                .keyboardShortcut("4", modifiers: [.command])
            }
        }
    }
}
