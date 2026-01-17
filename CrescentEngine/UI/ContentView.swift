import SwiftUI
import UniformTypeIdentifiers
import AppKit
import simd
import Combine
struct ContentView: View {
    @ObservedObject var editorState: EditorState
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [EditorTheme.backgroundTop, EditorTheme.backgroundBottom],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()
            
            RadialGradient(
                gradient: Gradient(colors: [EditorTheme.textAccent.opacity(0.18), Color.clear]),
                center: .topTrailing,
                startRadius: 20,
                endRadius: 420
            )
            .blendMode(.screen)
            .ignoresSafeArea()
            
            VStack(spacing: 0) {
                EditorToolbar(editorState: editorState)
                
                Divider()
                    .overlay(EditorTheme.panelStroke)

                HStack(spacing: 10) {
                    HierarchyPanel(editorState: editorState)
                        .frame(width: 260)

                    VStack(spacing: 10) {
                        SceneViewport(editorState: editorState)
                            .frame(minHeight: 320)
                            .layoutPriority(1)
                        
                        DockPanel(editorState: editorState)
                            .frame(height: 340)
                            .clipped()
                    }
                    .frame(minWidth: 560, maxWidth: .infinity, maxHeight: .infinity)

                    InspectorPanel(editorState: editorState)
                        .frame(width: 320)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding([.horizontal, .bottom], 10)
            }
            if !editorState.hasProject {
                ProjectLauncherView(editorState: editorState)
            }
        }
        .frame(minWidth: 1024, minHeight: 768)
        .environment(\.colorScheme, .dark)
        .onAppear {
            EditorTheme.isDark = true
            editorState.showHierarchy = true
            editorState.showInspector = true
            editorState.showAssets = true
            editorState.showConsole = true
            syncDockSelection()
            editorState.refreshProjectInfo()
        }
        .onChange(of: editorState.showAssets) { _ in
            syncDockSelection()
        }
        .onChange(of: editorState.showConsole) { _ in
            syncDockSelection()
        }
    }
    
    private func syncDockSelection() {
        if editorState.showAssets && !editorState.showConsole {
            editorState.dockTab = .assets
        } else if editorState.showConsole && !editorState.showAssets {
            editorState.dockTab = .console
        }
    }
}

struct ProjectLauncherView: View {
    @ObservedObject var editorState: EditorState
    @State private var projectName: String = ""
    
    var body: some View {
        ZStack {
            Color.black.opacity(0.55)
                .ignoresSafeArea()
            
            VStack(alignment: .leading, spacing: 16) {
                Text("CrescentEngine")
                    .font(EditorTheme.font(size: 24, weight: .bold))
                    .foregroundColor(EditorTheme.textPrimary)
                
                Text("Create a project folder to keep assets, scenes, and settings together.")
                    .font(EditorTheme.font(size: 12))
                    .foregroundColor(EditorTheme.textMuted)
                
                VStack(alignment: .leading, spacing: 8) {
                    Text("Project Name")
                        .font(EditorTheme.font(size: 11, weight: .semibold))
                        .foregroundColor(EditorTheme.textMuted)
                    TextField("MyGame", text: $projectName)
                        .textFieldStyle(.plain)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 8)
                        .background(EditorTheme.surface)
                        .cornerRadius(8)
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(EditorTheme.panelStroke, lineWidth: 1)
                        )
                }
                
                HStack(spacing: 12) {
                    Button("New Project") {
                        let panel = NSOpenPanel()
                        panel.canChooseDirectories = true
                        panel.canChooseFiles = false
                        panel.allowsMultipleSelection = false
                        panel.prompt = "Create"
                        let response = panel.runModal()
                        if response == .OK, let url = panel.url {
                            let name = projectName.isEmpty ? url.lastPathComponent : projectName
                            let rootURL = projectName.isEmpty ? url : url.appendingPathComponent(name)
                            editorState.createProject(at: rootURL, name: name)
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    
                    Button("Open Project") {
                        let panel = NSOpenPanel()
                        panel.allowsMultipleSelection = false
                        panel.canChooseFiles = true
                        panel.canChooseDirectories = true
                        panel.allowedFileTypes = ["cproj"]
                        panel.allowsOtherFileTypes = true
                        panel.prompt = "Open"
                        let response = panel.runModal()
                        if response == .OK, let url = panel.url {
                            editorState.openProject(at: url)
                        }
                    }
                    .buttonStyle(.bordered)
                }
            }
            .padding(20)
            .frame(width: 420)
            .background(EditorTheme.panelBackground)
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 14))
        }
    }
}

struct ConsolePanel: View {
    @ObservedObject var editorState: EditorState
    @State private var filterText = ""
    
    var filteredLogs: [ConsoleLog] {
        if filterText.isEmpty {
            return editorState.consoleLogs
        }
        return editorState.consoleLogs.filter { $0.message.localizedCaseInsensitiveContains(filterText) }
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Console")
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
                
                Spacer()
                
                HStack(spacing: 4) {
                    Image(systemName: "magnifyingglass")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(EditorTheme.textMuted)
                    TextField("Filter", text: $filterText)
                        .font(EditorTheme.fontBody)
                        .frame(width: 140)
                        .textFieldStyle(.plain)
                        .padding(.vertical, 4)
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(EditorTheme.surface)
                .cornerRadius(4)
                
                Button(action: {
                    editorState.clearConsole()
                }) {
                    Image(systemName: "trash")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(EditorTheme.textMuted)
                }
                .buttonStyle(.borderless)
                .help("Clear Console")
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(EditorTheme.panelHeader)
            
            Divider()
                .overlay(EditorTheme.panelStroke)
            
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 2) {
                        ForEach(filteredLogs) { log in
                            ConsoleMessage(log: log)
                                .id(log.id)
                        }
                    }
                    .padding(10)
                }
                .background(EditorTheme.backgroundBottom.opacity(0.75))
                .onChange(of: editorState.consoleLogs.count) { _ in
                    if let lastLog = editorState.consoleLogs.last {
                        withAnimation {
                            proxy.scrollTo(lastLog.id, anchor: .bottom)
                        }
                    }
                }
            }
        }
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}

struct ConsoleMessage: View {
    let log: ConsoleLog
    
    var body: some View {
        HStack(spacing: 6) {
            // Timestamp
            Text(log.timestamp, style: .time)
                .font(EditorTheme.fontCaption)
                .foregroundColor(.secondary)
                .frame(width: 70, alignment: .leading)
            
            // Icon
            Image(systemName: log.type.icon)
                .font(EditorTheme.fontCaption)
                .foregroundColor(log.type.color)
                .frame(width: 16)
            
            // Message
            Text(log.message)
                .font(EditorTheme.fontMono)
                .foregroundColor(.primary)
            
            Spacer()
        }
        .padding(.vertical, 2)
        .padding(.horizontal, 4)
        .background(log.type == .error ? Color.red.opacity(0.12) : Color.clear)
    }
}

struct ViewportPanel: View {
    @ObservedObject var editorState: EditorState
    let title: String
    let systemImage: String
    let viewKind: RenderViewKind
    let drivesLoop: Bool
    let allowsDrop: Bool
    @Binding var isDropping: Bool
    let onDrop: ([NSItemProvider]) -> Bool
    @State private var viewportSize: CGSize = .zero
    @State private var cameraBasis: CameraBasis = .identity
    private let cameraTimer = Timer.publish(every: 0.15, on: .main, in: .common).autoconnect()

    private var isActive: Bool {
        switch viewKind {
        case .scene: return editorState.viewMode == .scene
        case .game: return editorState.viewMode == .game
        }
    }

    var body: some View {
        GeometryReader { geometry in
            let headerColor = isActive ? EditorTheme.textAccent.opacity(0.25) : EditorTheme.panelHeader
            let borderColor = isActive ? EditorTheme.textAccent : EditorTheme.panelStroke

            let content = ZStack(alignment: .topLeading) {
                MetalView(viewKind: viewKind, isActive: isActive, drivesLoop: drivesLoop)
                    .frame(width: geometry.size.width, height: geometry.size.height)
                    .background(Color.black.opacity(0.6))
                    .clipped()
                    .onChange(of: geometry.size) { newValue in
                        viewportSize = newValue
                    }
                    .onAppear {
                        viewportSize = geometry.size
                    }

                HStack(spacing: 10) {
                    Label(title, systemImage: systemImage)
                        .font(EditorTheme.fontBodyMedium)

                    Text("\(Int(viewportSize.width)) x \(Int(viewportSize.height))")
                        .font(EditorTheme.fontMono)
                        .foregroundColor(EditorTheme.textMuted)

                    if isActive {
                        Text("Active")
                            .font(EditorTheme.fontCaption)
                            .foregroundColor(EditorTheme.textAccent)
                    }

                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(headerColor)
                .cornerRadius(6)
                .padding(10)
                .allowsHitTesting(false)

                if viewKind == .scene {
                    SceneCameraCompass(basis: cameraBasis)
                        .padding(10)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                        .allowsHitTesting(false)
                }

                if allowsDrop && isDropping {
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(EditorTheme.textAccent, style: StrokeStyle(lineWidth: 2, dash: [6]))
                        .background(EditorTheme.textAccent.opacity(0.08))
                }
            }

            let styled = content
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(EditorTheme.panelBackground)
                .overlay(
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(borderColor, lineWidth: 1)
                )
                .clipShape(RoundedRectangle(cornerRadius: 10))
                .onTapGesture {
                    if editorState.isPlaying {
                        return
                    }
                    switch viewKind {
                    case .scene:
                        editorState.setViewMode(.scene)
                    case .game:
                        editorState.setViewMode(.game)
                    }
                }

            if allowsDrop {
                styled.onDrop(of: [UTType.fileURL], isTargeted: $isDropping) { providers in
                    onDrop(providers)
                }
            } else {
                styled
            }
        }
        .onAppear {
            refreshCameraBasis()
        }
        .onReceive(cameraTimer) { _ in
            refreshCameraBasis()
        }
    }

    private func refreshCameraBasis() {
        guard viewKind == .scene else { return }
        let info = CrescentEngineBridge.shared().getSceneCameraBasis()
        guard let right = info["right"] as? [NSNumber], right.count >= 3,
              let up = info["up"] as? [NSNumber], up.count >= 3,
              let forward = info["forward"] as? [NSNumber], forward.count >= 3 else {
            return
        }

        cameraBasis = CameraBasis(
            right: simd_float3(right[0].floatValue, right[1].floatValue, right[2].floatValue),
            up: simd_float3(up[0].floatValue, up[1].floatValue, up[2].floatValue),
            forward: simd_float3(forward[0].floatValue, forward[1].floatValue, forward[2].floatValue)
        )
    }
}

private struct CameraBasis {
    var right: simd_float3
    var up: simd_float3
    var forward: simd_float3

    static let identity = CameraBasis(
        right: simd_float3(1, 0, 0),
        up: simd_float3(0, 1, 0),
        forward: simd_float3(0, 0, -1)
    )
}

private struct SceneCameraCompass: View {
    let basis: CameraBasis

    var body: some View {
        GeometryReader { geo in
            let size = min(geo.size.width, geo.size.height)
            let center = CGPoint(x: size * 0.5, y: size * 0.5)
            let radius = size * 0.32
            let axes = [
                AxisMarker(name: "X", color: .red, axis: simd_float3(1, 0, 0)),
                AxisMarker(name: "Y", color: .green, axis: simd_float3(0, 1, 0)),
                AxisMarker(name: "Z", color: .blue, axis: simd_float3(0, 0, 1))
            ]

            ZStack {
                Circle()
                    .fill(Color.black.opacity(0.25))
                    .overlay(
                        Circle().stroke(EditorTheme.panelStroke, lineWidth: 1)
                    )

                ForEach(axes) { axis in
                    let projected = project(axis.axis, center: center, radius: radius)
                    let alpha = projected.depth > 0 ? 0.95 : 0.4

                    Path { path in
                        path.move(to: center)
                        path.addLine(to: projected.point)
                    }
                    .stroke(axis.color.opacity(alpha), lineWidth: 2)

                    Text(axis.name)
                        .font(EditorTheme.fontMono)
                        .foregroundColor(axis.color.opacity(alpha))
                        .position(projected.point)
                }
            }
        }
        .frame(width: 72, height: 72)
    }

    private func project(_ axis: simd_float3, center: CGPoint, radius: CGFloat) -> ProjectedAxis {
        let x = simd_dot(axis, basis.right)
        let y = simd_dot(axis, basis.up)
        let z = simd_dot(axis, basis.forward)
        let point = CGPoint(
            x: center.x + CGFloat(x) * radius,
            y: center.y - CGFloat(y) * radius
        )
        return ProjectedAxis(point: point, depth: z)
    }
}

private struct AxisMarker: Identifiable {
    let id = UUID()
    let name: String
    let color: Color
    let axis: simd_float3
}

private struct ProjectedAxis {
    let point: CGPoint
    let depth: Float
}

struct ViewportTabBar: View {
    @ObservedObject var editorState: EditorState

    var body: some View {
        HStack(spacing: 8) {
            ForEach(ViewMode.allCases, id: \.self) { mode in
                let isActive = editorState.viewMode == mode
                Button {
                    editorState.setViewMode(mode)
                } label: {
                    Label(mode.label, systemImage: mode.systemImage)
                        .font(EditorTheme.fontBodyMedium)
                        .foregroundColor(isActive ? EditorTheme.textPrimary : EditorTheme.textMuted)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(isActive ? EditorTheme.surfaceElevated : EditorTheme.surface)
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(isActive ? EditorTheme.textAccent : EditorTheme.panelStroke, lineWidth: 1)
                        )
                        .cornerRadius(8)
                }
                .buttonStyle(.plain)
            }

            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(EditorTheme.panelHeader)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }
}

struct SceneViewport: View {
    @ObservedObject var editorState: EditorState
    @State private var isDroppingScene: Bool = false
    private let hdriExtensions = ["hdr", "exr"]
    
    var body: some View {
        VStack(spacing: 8) {
            ViewportTabBar(editorState: editorState)

            ZStack {
                ViewportPanel(
                    editorState: editorState,
                    title: "Scene",
                    systemImage: "cube.transparent",
                    viewKind: .scene,
                    drivesLoop: true,
                    allowsDrop: editorState.viewMode == .scene,
                    isDropping: $isDroppingScene,
                    onDrop: handleDrop
                )
                .opacity(editorState.viewMode == .scene ? 1.0 : 0.0)
                .allowsHitTesting(editorState.viewMode == .scene)

                ViewportPanel(
                    editorState: editorState,
                    title: "Game",
                    systemImage: "play.tv",
                    viewKind: .game,
                    drivesLoop: false,
                    allowsDrop: false,
                    isDropping: .constant(false),
                    onDrop: { _ in false }
                )
                .opacity(editorState.viewMode == .game ? 1.0 : 0.0)
                .allowsHitTesting(editorState.viewMode == .game)
            }
        }
        .onAppear {
            editorState.setViewMode(editorState.viewMode)
        }
    }
    
    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        for provider in providers where provider.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) {
            provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                DispatchQueue.main.async {
                    if let data = item as? Data,
                       let url = URL(dataRepresentation: data, relativeTo: nil) {
                        handleDropURL(url)
                    } else if let url = item as? URL {
                        handleDropURL(url)
                    }
                }
            }
        }
        return true
    }

    private func handleDropURL(_ url: URL) {
        var resolvedURL = url
        let accessed = resolvedURL.startAccessingSecurityScopedResource()
        if accessed {
            resolvedURL = resolvedURL.standardizedFileURL
        }
        
        defer {
            if accessed {
                resolvedURL.stopAccessingSecurityScopedResource()
            }
        }
        
        if isHDRIFile(resolvedURL) {
            var path = resolvedURL.path
            let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "hdri")
            if !imported.isEmpty {
                path = imported
            }
            if CrescentEngineBridge.shared().loadEnvironmentMap(path) {
                editorState.addLog(.info, "Skybox set: \(resolvedURL.lastPathComponent)")
            } else {
                editorState.addLog(.error, "Failed to load HDRI: \(resolvedURL.lastPathComponent)")
            }
            return
        }
        
        if editorState.isModelFile(resolvedURL) {
            editorState.importModel(from: resolvedURL)
            return
        }
        
        editorState.addLog(.warning, "Unsupported drop: \(resolvedURL.lastPathComponent)")
    }

    private func isHDRIFile(_ url: URL) -> Bool {
        hdriExtensions.contains(url.pathExtension.lowercased())
    }
}

#Preview {
    ContentView(editorState: EditorState())
        .frame(width: 1280, height: 800)
}
