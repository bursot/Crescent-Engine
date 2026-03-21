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
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                ToolbarCluster {
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
                        ToolbarMenuLabel(title: "Scene", systemImage: "square.stack.3d.up")
                    }
                    .menuStyle(.borderlessButton)

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
                        ToolbarMenuLabel(title: "GameObject", systemImage: "cube.fill")
                    }
                    .menuStyle(.borderlessButton)
                }

                ToolbarCluster {
                    Button {
                        cameraSpeed = CrescentEngineBridge.shared().getCameraMoveSpeed()
                        showCameraPopover.toggle()
                    } label: {
                        ToolbarPillLabel(
                            title: "Camera",
                            value: String(format: "%.0f", cameraSpeed),
                            systemImage: "video.fill"
                        )
                    }
                    .buttonStyle(.plain)
                    .help("Camera Settings")
                    .popover(isPresented: $showCameraPopover) {
                        CameraSettingsPopover(cameraSpeed: $cameraSpeed)
                            .padding(14)
                            .background(Color.clear)
                            .presentationBackground(.clear)
                    }

                    Button {
                        editorState.terrainPaintEnabled.toggle()
                        if !editorState.terrainPaintEnabled {
                            CrescentEngineBridge.shared().endTerrainPaint()
                        }
                    } label: {
                        ToolbarPillLabel(
                            title: "Terrain Brush",
                            value: editorState.terrainPaintEnabled ? "On" : "Off",
                            systemImage: "paintbrush.pointed.fill",
                            accent: editorState.terrainPaintEnabled ? EditorTheme.accentSecondary : EditorTheme.textMuted.opacity(0.9)
                        )
                    }
                    .buttonStyle(.plain)
                    .disabled(editorState.viewMode != .scene || editorState.primarySelectionUUID == nil)
                    .opacity(editorState.viewMode == .scene && editorState.primarySelectionUUID != nil ? 1 : 0.55)
                    .help("Enable terrain brush mode (Scene view only)")
                }

                ToolbarCluster {
                    Button(action: {
                        editorState.bakeLighting()
                    }) {
                        ToolbarPillLabel(
                            title: "Bake Lighting",
                            value: "Static",
                            systemImage: "sun.max.fill",
                            accent: EditorTheme.warning
                        )
                    }
                    .buttonStyle(.plain)
                    .disabled(!editorState.hasProject || editorState.isBuildingGame)
                    .opacity(editorState.hasProject && !editorState.isBuildingGame ? 1 : 0.55)
                    .help("Bake static lighting, lightmaps, probes and shadowmasks for the current scene")

                    Button(action: {
                        editorState.buildGame()
                    }) {
                        ToolbarPillLabel(
                            title: editorState.isBuildingGame ? "Building" : "Build Game",
                            value: editorState.isBuildingGame ? "..." : "Cooked+Bake",
                            systemImage: editorState.isBuildingGame ? "shippingbox.circle.fill" : "shippingbox.fill",
                            accent: editorState.isBuildingGame ? EditorTheme.warning : EditorTheme.textAccent
                        )
                    }
                    .buttonStyle(.plain)
                    .disabled(!editorState.hasProject || editorState.isBuildingGame)
                    .opacity(editorState.hasProject && !editorState.isBuildingGame ? 1 : 0.55)
                    .help("Build the current scene as a standalone game app with cooked textures and baked lighting")

                    Button(action: {
                        editorState.togglePlay()
                    }) {
                        ToolbarIconButtonLabel(
                            systemImage: editorState.isPlaying ? "stop.fill" : "play.fill",
                            tint: editorState.isPlaying ? EditorTheme.danger : EditorTheme.success
                        )
                    }
                    .buttonStyle(.plain)
                    .help(editorState.isPlaying ? "Stop" : "Play")

                    Button(action: {
                        editorState.togglePause()
                    }) {
                        ToolbarIconButtonLabel(
                            systemImage: "pause.fill",
                            tint: editorState.isPaused ? EditorTheme.warning : EditorTheme.textMuted
                        )
                    }
                    .buttonStyle(.plain)
                    .help(editorState.isPaused ? "Resume" : "Pause")
                    .disabled(!editorState.isPlaying)
                    .opacity(editorState.isPlaying ? 1 : 0.55)

                    HStack(spacing: 8) {
                        Text(String(format: "%.2fx", editorState.timeScale))
                            .font(EditorTheme.fontMono)
                            .foregroundColor(EditorTheme.textPrimary)
                            .frame(width: 46, alignment: .trailing)
                        Slider(value: $editorState.timeScale, in: 0.0...2.0, step: 0.05)
                            .tint(EditorTheme.textAccent)
                            .frame(width: 110)
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .editorInput(cornerRadius: 14)
                    .disabled(!editorState.isPlaying)
                    .opacity(editorState.isPlaying ? 1 : 0.55)
                    .onChange(of: editorState.timeScale) { _ in
                        editorState.applyTimeScale()
                    }
                    .help("Time Scale")
                }

                Button(action: {
                    editorState.settingsWindowRequested = true
                    openWindow(id: "settings")
                }) {
                    ToolbarIconButtonLabel(systemImage: "gearshape.fill", tint: EditorTheme.textPrimary)
                }
                .buttonStyle(.plain)
                .help("Open Settings")
            }
            .padding(.horizontal, 2)
            .padding(.vertical, 1)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
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
            HStack {
                Image(systemName: "video.fill")
                    .foregroundColor(EditorTheme.textAccent)
                Text("Camera")
                    .font(EditorTheme.font(size: 13, weight: .semibold))
            }
            
            Divider()
            
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Move Speed")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(EditorTheme.textMuted)
                    Spacer()
                    Text(String(format: "%.1f", cameraSpeed))
                        .font(EditorTheme.fontMono)
                        .foregroundColor(EditorTheme.textPrimary)
                }
                
                Slider(value: Binding(
                    get: { Double(cameraSpeed) },
                    set: { newValue in
                        cameraSpeed = Float(newValue)
                        CrescentEngineBridge.shared().setCameraMoveSpeed(cameraSpeed)
                    }
                ), in: 1...50)
                
                HStack(spacing: 6) {
                    ForEach([5, 10, 25, 50], id: \.self) { preset in
                        Button("\(preset)") {
                            cameraSpeed = Float(preset)
                            CrescentEngineBridge.shared().setCameraMoveSpeed(cameraSpeed)
                        }
                        .buttonStyle(.plain)
                        .font(EditorTheme.mono(size: 10))
                        .foregroundColor(Int(cameraSpeed.rounded()) == preset ? EditorTheme.textPrimary : EditorTheme.textMuted)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 6)
                        .background(
                            RoundedRectangle(cornerRadius: 10, style: .continuous)
                                .fill(Int(cameraSpeed.rounded()) == preset ? EditorTheme.textAccent.opacity(0.22) : EditorTheme.surfaceMuted.opacity(0.92))
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 10, style: .continuous)
                                .stroke(Int(cameraSpeed.rounded()) == preset ? EditorTheme.textAccent.opacity(0.5) : EditorTheme.panelStroke, lineWidth: 1)
                        )
                    }
                }
            }
            
            Text("Hold right-click + WASD to move")
                .font(EditorTheme.fontCaption)
                .foregroundColor(EditorTheme.textMuted)
        }
        .frame(width: 200)
        .editorPanel(cornerRadius: 18)
        .compositingGroup()
    }
}

private struct ToolbarCluster<Content: View>: View {
    @ViewBuilder let content: Content

    var body: some View {
        HStack(spacing: 8) {
            content
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .editorSection(cornerRadius: 16)
    }
}

private struct ToolbarMenuLabel: View {
    let title: String
    let systemImage: String

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: systemImage)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            Text(title)
                .font(EditorTheme.font(size: 13, weight: .semibold))
            Image(systemName: "chevron.down")
                .font(EditorTheme.font(size: 9, weight: .bold))
                .foregroundColor(EditorTheme.textMuted)
        }
        .foregroundColor(EditorTheme.textPrimary)
        .padding(.horizontal, 9)
        .padding(.vertical, 7)
        .editorInput(cornerRadius: 14)
    }
}

private struct ToolbarPillLabel: View {
    let title: String
    let value: String
    let systemImage: String
    var accent: Color = EditorTheme.textAccent

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: systemImage)
                .font(EditorTheme.font(size: 11, weight: .semibold))
                .foregroundColor(accent)
            Text(title)
                .font(EditorTheme.font(size: 12, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)
            Text(value)
                .font(EditorTheme.mono(size: 10))
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(.horizontal, 9)
        .padding(.vertical, 7)
        .editorInput(cornerRadius: 14)
    }
}

private struct ToolbarIconButtonLabel: View {
    let systemImage: String
    let tint: Color

    var body: some View {
        Image(systemName: systemImage)
            .font(EditorTheme.font(size: 13, weight: .bold))
            .foregroundColor(tint)
            .frame(width: 34, height: 34)
            .background(EditorTheme.inputGradient)
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
}
