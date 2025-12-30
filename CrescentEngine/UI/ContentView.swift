import SwiftUI
import UniformTypeIdentifiers
import AppKit

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
                
                HSplitView {
                    if editorState.showHierarchy {
                        HierarchyPanel(editorState: editorState)
                            .frame(minWidth: 220, idealWidth: 260, maxWidth: 340)
                    }
                    
                    VSplitView {
                        SceneViewport(editorState: editorState)
                            .frame(minHeight: 320)
                            .layoutPriority(1)
                        
                        if editorState.showAssets || editorState.showConsole {
                            DockPanel(editorState: editorState)
                                .frame(minHeight: 240, idealHeight: 300, maxHeight: 480)
                                .clipped()
                        }
                    }
                    .frame(minWidth: 560)
                    
                    if editorState.showInspector {
                        InspectorPanel(editorState: editorState)
                            .frame(minWidth: 260, idealWidth: 320, maxWidth: 460)
                    }
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

struct SceneViewport: View {
    @ObservedObject var editorState: EditorState
    @State private var viewportSize: CGSize = .zero
    @State private var isDropping: Bool = false
    private let hdriExtensions = ["hdr", "exr"]
    
    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topLeading) {
                MetalView()
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
                    Label("Scene", systemImage: "cube.transparent")
                        .font(EditorTheme.fontBodyMedium)
                    
                    Text("\(Int(viewportSize.width)) x \(Int(viewportSize.height))")
                        .font(EditorTheme.fontMono)
                        .foregroundColor(EditorTheme.textMuted)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(EditorTheme.panelHeader)
                .cornerRadius(6)
                .padding(10)
                .allowsHitTesting(false)
                
                if isDropping {
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(EditorTheme.textAccent, style: StrokeStyle(lineWidth: 2, dash: [6]))
                        .background(EditorTheme.textAccent.opacity(0.08))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(EditorTheme.panelBackground)
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .onDrop(of: [UTType.fileURL], isTargeted: $isDropping) { providers in
                handleDrop(providers)
            }
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
