import SwiftUI
import UniformTypeIdentifiers
import AppKit

struct HierarchyPanel: View {
    @ObservedObject var editorState: EditorState
    @State private var searchText: String = ""
    @State private var showModelImporter: Bool = false
    
    private let modelTypes: [UTType] = {
        let exts = [
            "fbx", "obj", "gltf", "glb", "dae", "blend", "3ds",
            "stl", "ply", "x", "smd", "md5mesh", "md2", "md3", "ms3d", "lwo", "lws"
        ]
        return exts.compactMap { UTType(filenameExtension: $0) }
    }()
    
    private var filteredEntities: [EntityInfo] {
        if searchText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return editorState.entityList
        }
        
        return editorState.entityList.filter { entity in
            entity.name.localizedCaseInsensitiveContains(searchText)
        }
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 8) {
                Label("Hierarchy", systemImage: "square.grid.2x2")
                    .labelStyle(.titleAndIcon)
                    .font(EditorTheme.fontBodyMedium)
                    .foregroundColor(EditorTheme.textPrimary)
                
                Spacer()
                
                Button(action: {
                    editorState.deleteSelected()
                }) {
                    Image(systemName: "trash")
                        .font(EditorTheme.fontBodyMedium)
                }
                .buttonStyle(.borderless)
                .foregroundColor(editorState.selectedEntityUUIDs.isEmpty ? EditorTheme.textMuted : .red)
                .help("Delete selection")
                .disabled(editorState.selectedEntityUUIDs.isEmpty)
                
                Button(action: {
                    CrescentEngineBridge.shared().createEmpty()
                    editorState.refreshEntityList()
                }) {
                    Image(systemName: "plus")
                        .font(EditorTheme.fontBodyMedium)
                        .foregroundColor(EditorTheme.textPrimary)
                }
                .buttonStyle(.borderless)
                .help("Create Empty GameObject")
                
                Button(action: {
                    showModelImporter = true
                }) {
                    Image(systemName: "square.and.arrow.down")
                        .font(EditorTheme.fontBodyMedium)
                        .foregroundColor(EditorTheme.textPrimary)
                }
                .buttonStyle(.borderless)
                .help("Import Model")
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(EditorTheme.panelHeader)
            
            Divider()
                .overlay(EditorTheme.panelStroke)
            
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 6) {
                    Image(systemName: "magnifyingglass")
                        .font(EditorTheme.fontBody)
                        .foregroundColor(EditorTheme.textMuted)
                    TextField("Search objects", text: $searchText)
                        .textFieldStyle(.plain)
                        .font(EditorTheme.fontBody)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(EditorTheme.surface)
                .cornerRadius(6)
                
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 4) {
                        ForEach(filteredEntities) { entity in  // ✅ Uses entity.uuid as ID!
                            HierarchyItem(
                                name: entity.name,
                                hasAnimator: entity.hasAnimator,
                                hasSkinned: entity.hasSkinned,
                                clipCount: entity.clipCount,
                                isSelected: editorState.selectedEntityUUIDs.contains(entity.uuid),  // ✅ Check by UUID!
                                onSelect: { isToggle in
                                    editorState.selectEntity(entity.uuid, toggle: isToggle)  // ✅ Select by UUID!
                                }
                            )
                        }
                    }
                    .padding(.vertical, 4)
                }
            }
            .padding(12)
        }
        .frame(minWidth: 200, maxWidth: 320)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .fileImporter(isPresented: $showModelImporter, allowedContentTypes: modelTypes, allowsMultipleSelection: false) { result in
            handleModelImport(result)
        }
    }
    
    private func handleModelImport(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            editorState.importModel(from: url)
        case .failure(let error):
            editorState.addLog(.error, "Model import failed: \(error.localizedDescription)")
        }
    }
}

struct HierarchyItem: View {
    let name: String
    let hasAnimator: Bool
    let hasSkinned: Bool
    let clipCount: Int
    let isSelected: Bool
    let onSelect: (_ isToggle: Bool) -> Void  // ✅ Pass toggle flag
    
    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: iconForEntity(name))
                .font(EditorTheme.fontBody)
                .foregroundColor(colorForEntity(name))
                .frame(width: 18)
            
            Text(name)
                .font(EditorTheme.fontBodyMedium)
                .foregroundColor(isSelected ? EditorTheme.textPrimary : EditorTheme.textMuted)

            if hasAnimator {
                Text("ANIM")
                    .font(EditorTheme.mono(size: 9))
                    .foregroundColor(Color.white.opacity(0.9))
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(Color.teal.opacity(0.75))
                    .cornerRadius(4)
            } else if hasSkinned {
                Text("SKIN")
                    .font(EditorTheme.mono(size: 9))
                    .foregroundColor(Color.white.opacity(0.85))
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(Color.blue.opacity(0.6))
                    .cornerRadius(4)
            }
            
            Spacer()
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(isSelected ? Color.accentColor.opacity(0.2) : Color.clear)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(isSelected ? Color.accentColor : EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(6)
        .contentShape(Rectangle())
        // ✅ Proper gesture handling for Cmd+Click
        .simultaneousGesture(
            TapGesture()
                .modifiers(.command)
                .onEnded { _ in
                    onSelect(true)  // Toggle mode (Cmd+Click)
                }
        )
        .onTapGesture {
            onSelect(false)  // Normal click (replace selection)
        }
    }
    
    private func iconForEntity(_ name: String) -> String {
        if hasAnimator {
            return "figure.walk"
        } else if name.contains("Camera") {
            return "video.fill"
        } else if name.contains("Light") {
            return "lightbulb.fill"
        } else if name.contains("Cube") || name.contains("Sphere") || 
                  name.contains("Plane") || name.contains("Cylinder") {
            return "cube.fill"
        } else {
            return "cube.transparent"
        }
    }
    
    private func colorForEntity(_ name: String) -> Color {
        if hasAnimator {
            return .teal
        } else if name.contains("Camera") {
            return .blue
        } else if name.contains("Light") {
            return .yellow
        } else {
            return .gray
        }
    }
}
