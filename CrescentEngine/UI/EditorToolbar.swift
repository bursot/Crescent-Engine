import SwiftUI

struct EditorToolbar: View {
    @ObservedObject var editorState: EditorState
    @Environment(\.openWindow) private var openWindow
    @State private var showCameraPopover: Bool = false
    @State private var cameraSpeed: Float = 5.0
    
    init(editorState: EditorState) {
        self.editorState = editorState
    }
    
    var body: some View {
        HStack(spacing: 12) {
            Menu {
                Button("Save Scene") {
                    editorState.saveScene()
                }
                Button("Save Scene As...") {
                    editorState.saveSceneAs()
                }
                Divider()
                Button("Open Scene...") {
                    editorState.loadScene()
                }
            } label: {
                Label("Scene", systemImage: "square.stack.3d.up")
                    .font(EditorTheme.fontBodyMedium)
            }
            .menuStyle(.borderlessButton)
            .frame(height: 28)
            
            // GameObject menu
            Menu {
                Menu("3D Object") {
                    Button("Cube") {
                        CrescentEngineBridge.shared().createCube()
                        editorState.refreshEntityList()
                    }
                    Button("Sphere") {
                        CrescentEngineBridge.shared().createSphere()
                        editorState.refreshEntityList()
                    }
                    Button("Plane") {
                        CrescentEngineBridge.shared().createPlane()
                        editorState.refreshEntityList()
                    }
                    Button("Cylinder") {
                        CrescentEngineBridge.shared().createCylinder()
                        editorState.refreshEntityList()
                    }
                    Button("Cone") {
                        CrescentEngineBridge.shared().createCone()
                        editorState.refreshEntityList()
                    }
                    Button("Torus") {
                        CrescentEngineBridge.shared().createTorus()
                        editorState.refreshEntityList()
                    }
                    Button("Capsule") {
                        CrescentEngineBridge.shared().createCapsule()
                        editorState.refreshEntityList()
                    }
                }
                
                Menu("Light") {
                    Button("Directional Light") {
                        CrescentEngineBridge.shared().createDirectionalLight()
                        editorState.refreshEntityList()
                    }
                    Button("Point Light") {
                        CrescentEngineBridge.shared().createPointLight()
                        editorState.refreshEntityList()
                    }
                    Button("Spot Light") {
                        CrescentEngineBridge.shared().createSpotLight()
                        editorState.refreshEntityList()
                    }
                }

                Menu("Effects") {
                    Button("Decal") {
                        CrescentEngineBridge.shared().createDecal()
                        editorState.refreshEntityList()
                    }
                }
                
                Divider()
                
                Button("Camera") {
                    CrescentEngineBridge.shared().createCamera()
                    editorState.refreshEntityList()
                }
                
                Divider()
                
                Button("Empty GameObject") {
                    CrescentEngineBridge.shared().createEmpty()
                    editorState.refreshEntityList()
                }
            } label: {
                Label("GameObject", systemImage: "cube.fill")
                    .font(EditorTheme.fontBodyMedium)
            }
            .menuStyle(.borderlessButton)
            .frame(height: 28)
            
            Divider()
                .frame(height: 20)
            
            // Camera settings button
            Button {
                cameraSpeed = CrescentEngineBridge.shared().getCameraMoveSpeed()
                showCameraPopover.toggle()
            } label: {
                HStack(spacing: 4) {
                    Image(systemName: "video.fill")
                        .font(EditorTheme.fontBody)
                    Text(String(format: "%.0f", cameraSpeed))
                        .font(EditorTheme.fontMono)
                }
            }
            .buttonStyle(.bordered)
            .help("Camera Settings")
            .popover(isPresented: $showCameraPopover) {
                CameraSettingsPopover(cameraSpeed: $cameraSpeed)
                    .padding(12)
            }
            
            Spacer()

            Button(action: {
                editorState.togglePlay()
            }) {
                Image(systemName: editorState.isPlaying ? "stop.fill" : "play.fill")
                    .font(EditorTheme.fontBody)
                    .frame(width: 28, height: 28)
            }
            .buttonStyle(.bordered)
            .tint(editorState.isPlaying ? .red : .green)
            .help(editorState.isPlaying ? "Stop" : "Play")
            
            // View toggles
            HStack(spacing: 6) {
                Button(action: {
                    editorState.showHierarchy.toggle()
                }) {
                    Image(systemName: "sidebar.left")
                        .font(EditorTheme.fontBody)
                }
                .buttonStyle(.bordered)
                .tint(editorState.showHierarchy ? .accentColor : .secondary)
                .help("Toggle Hierarchy")
                
                Button(action: {
                    editorState.showConsole.toggle()
                }) {
                    Image(systemName: "terminal")
                        .font(EditorTheme.fontBody)
                }
                .buttonStyle(.bordered)
                .tint(editorState.showConsole ? .accentColor : .secondary)
                .help("Toggle Console")
                
                Button(action: {
                    editorState.showAssets.toggle()
                }) {
                    Image(systemName: "tray.full")
                        .font(EditorTheme.fontBody)
                }
                .buttonStyle(.bordered)
                .tint(editorState.showAssets ? .accentColor : .secondary)
                .help("Toggle Assets")
                
                Button(action: {
                    editorState.showInspector.toggle()
                }) {
                    Image(systemName: "sidebar.right")
                        .font(EditorTheme.fontBody)
                }
                .buttonStyle(.bordered)
                .tint(editorState.showInspector ? .accentColor : .secondary)
                .help("Toggle Inspector")

                Button(action: {
                    editorState.settingsWindowRequested = true
                    openWindow(id: "settings")
                }) {
                    Image(systemName: "gearshape")
                        .font(EditorTheme.fontBody)
                }
                .buttonStyle(.bordered)
                .help("Open Settings")
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(EditorTheme.panelHeader)
        .overlay(
            Rectangle()
                .frame(height: 1)
                .foregroundColor(EditorTheme.panelStroke),
            alignment: .bottom
        )
        .onAppear {
            cameraSpeed = CrescentEngineBridge.shared().getCameraMoveSpeed()
        }
    }
}

// MARK: - Camera Settings Popover

struct CameraSettingsPopover: View {
    @Binding var cameraSpeed: Float
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Header
            HStack {
                Image(systemName: "video.fill")
                    .foregroundColor(.blue)
                Text("Camera")
                    .font(EditorTheme.fontBodyMedium)
            }
            
            Divider()
            
            // Speed control
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Move Speed")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(.secondary)
                    Spacer()
                    Text(String(format: "%.1f", cameraSpeed))
                        .font(EditorTheme.fontMono)
                        .foregroundColor(.primary)
                }
                
                Slider(value: Binding(
                    get: { Double(cameraSpeed) },
                    set: { newValue in
                        cameraSpeed = Float(newValue)
                        CrescentEngineBridge.shared().setCameraMoveSpeed(cameraSpeed)
                    }
                ), in: 1...50)
                
                // Preset buttons
                HStack(spacing: 6) {
                    ForEach([5, 10, 25, 50], id: \.self) { preset in
                        Button("\(preset)") {
                            cameraSpeed = Float(preset)
                            CrescentEngineBridge.shared().setCameraMoveSpeed(cameraSpeed)
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .tint(Int(cameraSpeed) == preset ? .blue : .secondary)
                    }
                }
            }
            
            // Info
                Text("Hold right-click + WASD to move")
                .font(EditorTheme.fontCaption)
                .foregroundColor(.secondary)
        }
        .frame(width: 200)
    }
}
