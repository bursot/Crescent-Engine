import SwiftUI
import UniformTypeIdentifiers
import AppKit
import simd
import Combine
struct ContentView: View {
    @ObservedObject var editorState: EditorState
    @State private var hierarchyWidthOverride: CGFloat?
    @State private var inspectorWidthOverride: CGFloat?
    @State private var dockHeightOverride: CGFloat?

    var body: some View {
        ZStack {
            EditorTheme.appBackdrop
                .ignoresSafeArea()

            GeometryReader { geometry in
                let metrics = EditorLayoutMetrics(
                    size: geometry.size,
                    hierarchyVisible: editorState.showHierarchy,
                    inspectorVisible: editorState.showInspector
                )
                let hierarchyWidth = resolvedHierarchyWidth(metrics: metrics, size: geometry.size)
                let inspectorWidth = resolvedInspectorWidth(metrics: metrics, size: geometry.size)
                let dockHeight = resolvedDockHeight(metrics: metrics, size: geometry.size)

                VStack(spacing: 0) {
                    EditorToolbar(editorState: editorState)
                        .padding(.horizontal, metrics.chromeInset)
                        .padding(.top, metrics.chromeInset)
                        .padding(.bottom, 6)

                    Rectangle()
                        .fill(
                            LinearGradient(
                                colors: [EditorTheme.panelStrokeStrong.opacity(0.9), EditorTheme.panelStroke.opacity(0.15)],
                                startPoint: .leading,
                                endPoint: .trailing
                            )
                        )
                        .frame(height: 1)

                    HStack(spacing: 0) {
                        if metrics.showsHierarchy {
                            HierarchyPanel(editorState: editorState)
                                .frame(width: hierarchyWidth)
                                .layoutPriority(0)
                                .overlay(alignment: .trailing) {
                                    if !editorState.hierarchyCollapsed {
                                        PanelResizeHandle(axis: .vertical, currentValue: hierarchyWidth) { startValue, delta in
                                            hierarchyWidthOverride = clamped(
                                                startValue + delta,
                                                min: 220,
                                                max: max(260, min(geometry.size.width * 0.34, 460))
                                            )
                                        }
                                        .offset(x: metrics.contentSpacing * 0.5)
                                    }
                                }

                        }

                        VStack(spacing: 0) {
                            SceneViewport(editorState: editorState)
                                .frame(minHeight: metrics.viewportMinHeight)
                                .layoutPriority(1)
                                .padding(.leading, metrics.showsHierarchy ? metrics.contentSpacing : 0)
                                .padding(.trailing, metrics.showsInspector ? metrics.contentSpacing : 0)
                                .zIndex(1)

                            DockPanel(editorState: editorState)
                                .frame(height: dockHeight)
                                .padding(.top, metrics.contentSpacing)
                                .padding(.leading, metrics.showsHierarchy ? metrics.contentSpacing : 0)
                                .padding(.trailing, metrics.showsInspector ? metrics.contentSpacing : 0)
                                .overlay(alignment: .top) {
                                    if !editorState.dockCollapsed {
                                        PanelResizeHandle(axis: .horizontal, currentValue: dockHeight) { startValue, delta in
                                            dockHeightOverride = clamped(
                                                startValue - delta,
                                                min: 260,
                                                max: resolvedDockMaxHeight(metrics: metrics, size: geometry.size)
                                            )
                                        }
                                        .offset(y: -(metrics.contentSpacing + 6))
                                        .zIndex(3)
                                    }
                                }
                                .zIndex(2)
                        }
                        .frame(maxWidth: .infinity, maxHeight: .infinity)

                        if metrics.showsInspector {
                            InspectorPanel(editorState: editorState)
                                .frame(width: inspectorWidth)
                                .layoutPriority(0)
                                .overlay(alignment: .leading) {
                                    if !editorState.inspectorCollapsed {
                                        PanelResizeHandle(axis: .vertical, currentValue: inspectorWidth) { startValue, delta in
                                            inspectorWidthOverride = clamped(
                                                startValue - delta,
                                                min: 260,
                                                max: max(300, min(geometry.size.width * 0.36, 500))
                                            )
                                        }
                                        .offset(x: -(metrics.contentSpacing * 0.5))
                                    }
                                }
                        }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                    .padding(metrics.contentInset)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .editorShell()
                .overlay(alignment: .topLeading) {
                    if !metrics.reduceVisualEffects {
                        RoundedRectangle(cornerRadius: EditorTheme.shellCornerRadius, style: .continuous)
                            .fill(
                                LinearGradient(
                                    colors: [Color.white.opacity(0.12), Color.clear],
                                    startPoint: .topLeading,
                                    endPoint: .bottomTrailing
                                )
                            )
                            .padding(1)
                            .blendMode(.screen)
                            .allowsHitTesting(false)
                    }
                }
                .padding(metrics.windowInset)
            }

            if !editorState.hasProject {
                ProjectLauncherView(editorState: editorState)
            }
        }
        .frame(minWidth: 860, minHeight: 640)
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

    private func resolvedHierarchyWidth(metrics: EditorLayoutMetrics, size: CGSize) -> CGFloat {
        if editorState.hierarchyCollapsed {
            return 220
        }
        let maxWidth = max(260, min(size.width * 0.34, 460))
        return clamped(hierarchyWidthOverride ?? metrics.hierarchyWidth, min: 220, max: maxWidth)
    }

    private func resolvedInspectorWidth(metrics: EditorLayoutMetrics, size: CGSize) -> CGFloat {
        if editorState.inspectorCollapsed {
            return 220
        }
        let maxWidth = max(300, min(size.width * 0.36, 500))
        return clamped(inspectorWidthOverride ?? metrics.inspectorWidth, min: 260, max: maxWidth)
    }

    private func resolvedDockHeight(metrics: EditorLayoutMetrics, size: CGSize) -> CGFloat {
        if editorState.dockCollapsed {
            return 64
        }
        let maxHeight = resolvedDockMaxHeight(metrics: metrics, size: size)
        return clamped(dockHeightOverride ?? metrics.dockHeight, min: 260, max: maxHeight)
    }

    private func resolvedDockMaxHeight(metrics: EditorLayoutMetrics, size: CGSize) -> CGFloat {
        let viewportReserve = max(190, metrics.viewportMinHeight)
        let chromeReserve = 132 + (metrics.windowInset * 2) + (metrics.contentInset * 2)
        let availableHeight = size.height - viewportReserve - chromeReserve
        return max(300, min(availableHeight, min(size.height * 0.56, 620)))
    }
}

struct ProjectLauncherView: View {
    @ObservedObject var editorState: EditorState
    @State private var projectName: String = ""
    
    var body: some View {
        ZStack {
            Rectangle()
                .fill(.ultraThinMaterial)
                .overlay(Color.black.opacity(0.48))
                .ignoresSafeArea()
            
            VStack(alignment: .leading, spacing: 16) {
                Text("CrescentEngine")
                    .font(EditorTheme.font(size: 30, weight: .bold))
                    .foregroundColor(EditorTheme.textPrimary)
                
                Text("Create a project folder to keep assets, scenes, and settings together.")
                    .font(EditorTheme.font(size: 13))
                    .foregroundColor(EditorTheme.textMuted)
                
                VStack(alignment: .leading, spacing: 8) {
                    Text("Project Name")
                        .font(EditorTheme.font(size: 11, weight: .semibold))
                        .foregroundColor(EditorTheme.textMuted)
                    TextField("MyGame", text: $projectName)
                        .textFieldStyle(.plain)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 8)
                        .editorInput()
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
            .padding(24)
            .frame(width: 440)
            .editorPanel(cornerRadius: 24)
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
                    .font(EditorTheme.font(size: 13, weight: .semibold))
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
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .editorInput()
                
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
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .background(EditorTheme.panelHeader.opacity(0.72))
            
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
                .background(
                    LinearGradient(
                        colors: [EditorTheme.surfaceMuted.opacity(0.82), EditorTheme.backgroundBottom.opacity(0.72)],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
                .onChange(of: editorState.consoleLogs.count) { _ in
                    if let lastLog = editorState.consoleLogs.last {
                        withAnimation {
                            proxy.scrollTo(lastLog.id, anchor: .bottom)
                        }
                    }
                }
            }
        }
        .editorPanel(cornerRadius: 18)
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
    private let cameraTimer = Timer.publish(every: 0.3, on: .main, in: .common).autoconnect()

    private var isActive: Bool {
        switch viewKind {
        case .scene: return editorState.viewMode == .scene
        case .game: return editorState.viewMode == .game
        }
    }

    var body: some View {
        GeometryReader { geometry in
            let headerColor = isActive ? EditorTheme.textAccent.opacity(0.18) : EditorTheme.panelHeader.opacity(0.82)
            let borderColor = isActive ? EditorTheme.textAccent.opacity(0.9) : EditorTheme.panelStrokeStrong

            let content = ZStack(alignment: .topLeading) {
                EditorTheme.viewportGradient
                    .overlay(
                        LinearGradient(
                            colors: [Color.white.opacity(0.08), Color.clear, Color.black.opacity(0.18)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )

                MetalView(
                    viewKind: viewKind,
                    isActive: isActive,
                    drivesLoop: drivesLoop,
                    terrainPaintConfig: editorState.terrainPaintConfig
                )
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
                        .font(EditorTheme.font(size: 13, weight: .semibold))

                    Text("\(Int(viewportSize.width)) x \(Int(viewportSize.height))")
                        .font(EditorTheme.fontMono)
                        .foregroundColor(EditorTheme.textMuted)

                    if isActive {
                        Text("Active")
                            .font(EditorTheme.fontCaption)
                            .foregroundColor(EditorTheme.textAccent)
                    }

                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(headerColor)
                .overlay(
                    Capsule(style: .continuous)
                        .stroke(isActive ? EditorTheme.textAccent.opacity(0.4) : EditorTheme.panelStroke, lineWidth: 1)
                )
                .clipShape(Capsule(style: .continuous))
                .padding(14)
                .allowsHitTesting(false)

                if viewKind == .game && editorState.isPlaying {
                    CrosshairView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .allowsHitTesting(false)
                }

                if viewKind == .scene {
                    SceneCameraCompass(basis: cameraBasis)
                        .padding(10)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                        .allowsHitTesting(false)
                }

                if viewKind == .scene && isActive && editorState.terrainPaintConfig.enabled {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Terrain Paint ON")
                            .font(EditorTheme.font(size: 10, weight: .semibold))
                        Text("Layer \(editorState.terrainPaintLayer)  Radius \(String(format: "%.2f", editorState.terrainBrushRadius))  Strength \(String(format: "%.2f", editorState.terrainBrushStrength))")
                            .font(EditorTheme.mono(size: 10))
                        Text("Hardness \(String(format: "%.2f", editorState.terrainBrushHardness))  Spacing \(String(format: "%.2f", editorState.terrainBrushSpacing))  Option=Erase")
                            .font(EditorTheme.mono(size: 10))
                            .foregroundColor(EditorTheme.textMuted)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(EditorTheme.surfaceElevated.opacity(0.9))
                    .overlay(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .stroke(EditorTheme.textAccent.opacity(0.8), lineWidth: 1)
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                    .padding(14)
                    .padding(.top, 36)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                    .allowsHitTesting(false)
                }

                if allowsDrop && isDropping {
                    RoundedRectangle(cornerRadius: 18, style: .continuous)
                        .stroke(EditorTheme.textAccent, style: StrokeStyle(lineWidth: 2, dash: [6]))
                        .background(EditorTheme.textAccent.opacity(0.08))
                }
            }

            let styled = content
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .overlay(
                    RoundedRectangle(cornerRadius: 18, style: .continuous)
                        .stroke(borderColor, lineWidth: 1)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 18, style: .continuous)
                        .inset(by: 1)
                        .stroke(Color.white.opacity(0.06), lineWidth: 1)
                )
                .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
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
            if isActive {
                refreshCameraBasis()
            }
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

private struct CrosshairView: View {
    @State private var lastFireCounter: UInt64 = 0
    @State private var pulse: CGFloat = 0.0

    private let pollTimer = Timer.publish(every: 1.0 / 60.0, on: .main, in: .common).autoconnect()

    var body: some View {
        let baseGap: CGFloat = 6.0
        let baseLength: CGFloat = 14.0
        let baseThickness: CGFloat = 2.0
        let gap = baseGap + 8.0 * pulse
        let length = baseLength + 6.0 * pulse
        let thickness = baseThickness + 1.0 * pulse

        ZStack {
            crosshairTicks(color: Color.black.opacity(0.45),
                           thickness: thickness + 1.0,
                           length: length + 1.0,
                           gap: gap)
            crosshairTicks(color: Color.white.opacity(0.9),
                           thickness: thickness,
                           length: length,
                           gap: gap)
        }
        .frame(width: 54, height: 54)
        .scaleEffect(1.0 + 0.04 * pulse)
        .shadow(color: Color.black.opacity(0.25), radius: 2, x: 0, y: 0)
        .onReceive(pollTimer) { _ in
            let counter = CrescentEngineBridge.shared().getFireEventCounter().uint64Value
            if counter != lastFireCounter {
                lastFireCounter = counter
                triggerPulse()
            }
        }
    }

    @ViewBuilder
    private func crosshairTicks(color: Color, thickness: CGFloat, length: CGFloat, gap: CGFloat) -> some View {
        let half = length * 0.5
        CrosshairTick(width: length, height: thickness, offset: CGSize(width: gap + half, height: 0), color: color)
        CrosshairTick(width: length, height: thickness, offset: CGSize(width: -(gap + half), height: 0), color: color)
        CrosshairTick(width: thickness, height: length, offset: CGSize(width: 0, height: gap + half), color: color)
        CrosshairTick(width: thickness, height: length, offset: CGSize(width: 0, height: -(gap + half)), color: color)
    }

    private func triggerPulse() {
        withAnimation(.easeOut(duration: 0.06)) {
            pulse = 1.0
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.08) {
            withAnimation(.easeIn(duration: 0.16)) {
                pulse = 0.0
            }
        }
    }
}

private struct CrosshairTick: View {
    let width: CGFloat
    let height: CGFloat
    let offset: CGSize
    let color: Color

    var body: some View {
        RoundedRectangle(cornerRadius: min(width, height) * 0.45)
            .fill(color)
            .frame(width: width, height: height)
            .offset(offset)
    }
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
                        .padding(.horizontal, 14)
                        .padding(.vertical, 8)
                        .background(isActive ? EditorTheme.surfaceElevated : EditorTheme.surfaceMuted)
                        .overlay(
                            Capsule(style: .continuous)
                                .stroke(isActive ? EditorTheme.textAccent : EditorTheme.panelStroke, lineWidth: 1)
                        )
                        .clipShape(Capsule(style: .continuous))
                }
                .buttonStyle(.plain)
            }

            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .editorSection()
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

private struct EditorLayoutMetrics {
    let showsHierarchy: Bool
    let showsInspector: Bool
    let reduceVisualEffects: Bool
    let windowInset: CGFloat
    let chromeInset: CGFloat
    let contentInset: CGFloat
    let contentSpacing: CGFloat
    let hierarchyWidth: CGFloat
    let inspectorWidth: CGFloat
    let dockHeight: CGFloat
    let viewportMinHeight: CGFloat

    init(size: CGSize, hierarchyVisible: Bool, inspectorVisible: Bool) {
        let compactWidth = size.width < 1360
        let narrowWidth = size.width < 1180

        showsInspector = inspectorVisible && !narrowWidth
        showsHierarchy = hierarchyVisible && size.width >= 1080
        reduceVisualEffects = compactWidth
        windowInset = clamped(size.width * 0.008, min: 6, max: 12)
        chromeInset = clamped(size.width * 0.006, min: 6, max: 10)
        contentInset = clamped(size.width * 0.008, min: 6, max: 10)
        contentSpacing = clamped(size.width * 0.006, min: 6, max: 10)

        let fullWidth = max(size.width - (windowInset * 2) - (contentInset * 2), 640)
        let sideFraction = fullWidth > 1500 ? 0.19 : 0.21
        hierarchyWidth = showsHierarchy ? clamped(fullWidth * (compactWidth ? 0.18 : sideFraction), min: 220, max: 320) : 0
        inspectorWidth = showsInspector ? clamped(fullWidth * (compactWidth ? 0.2 : sideFraction), min: 260, max: 360) : 0
        dockHeight = clamped(size.height * (compactWidth ? 0.33 : 0.37), min: 280, max: 440)
        viewportMinHeight = clamped(size.height * (compactWidth ? 0.28 : 0.31), min: 210, max: 360)
    }
}

private struct PanelResizeHandle: View {
    enum Axis {
        case vertical
        case horizontal
    }

    let axis: Axis
    let currentValue: CGFloat
    let onDrag: (CGFloat, CGFloat) -> Void
    @State private var dragStartValue: CGFloat?
    @State private var isHovering: Bool = false

    var body: some View {
        Group {
            if axis == .vertical {
                Rectangle()
                    .fill(Color.clear)
                    .frame(width: 12)
                    .frame(maxHeight: .infinity)
            } else {
                Rectangle()
                    .fill(Color.clear)
                    .frame(height: 12)
                    .frame(maxWidth: .infinity)
            }
        }
        .contentShape(Rectangle())
        .onHover { hovering in
            updateCursor(hovering)
        }
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { value in
                    let startValue = dragStartValue ?? currentValue
                    dragStartValue = startValue
                    switch axis {
                    case .vertical:
                        onDrag(startValue, value.translation.width)
                    case .horizontal:
                        onDrag(startValue, value.translation.height)
                    }
                }
                .onEnded { _ in
                    dragStartValue = nil
                }
        )
        .onDisappear {
            if isHovering {
                NSCursor.pop()
                isHovering = false
            }
        }
    }

    private func updateCursor(_ hovering: Bool) {
        guard hovering != isHovering else { return }
        isHovering = hovering
        switch (axis, hovering) {
        case (.vertical, true):
            NSCursor.resizeLeftRight.push()
        case (.horizontal, true):
            NSCursor.resizeUpDown.push()
        case (_, false):
            NSCursor.pop()
        }
    }
}

private func clamped(_ value: CGFloat, min minValue: CGFloat, max maxValue: CGFloat) -> CGFloat {
    Swift.max(minValue, Swift.min(maxValue, value))
}

#Preview {
    ContentView(editorState: EditorState())
        .frame(width: 1280, height: 800)
}
