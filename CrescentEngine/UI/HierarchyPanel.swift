import SwiftUI
import UniformTypeIdentifiers
import AppKit

private struct HierarchyNode: Identifiable {
    let entity: EntityInfo
    var children: [HierarchyNode]

    var id: String { entity.uuid }
}

private struct HierarchyDropDelegate: DropDelegate {
    let targetUUID: String?
    let canDrop: () -> Bool
    let performDropAction: () -> Bool
    @Binding var dropTargetUUID: String?
    @Binding var isDragging: Bool
    @Binding var draggingUUIDs: [String]

    func validateDrop(info: DropInfo) -> Bool {
        canDrop()
    }

    func dropEntered(info: DropInfo) {
        if canDrop() {
            dropTargetUUID = targetUUID
        }
        isDragging = true
    }

    func dropExited(info: DropInfo) {
        if dropTargetUUID == targetUUID {
            dropTargetUUID = nil
        }
    }

    func dropUpdated(info: DropInfo) -> DropProposal? {
        DropProposal(operation: .move)
    }

    func performDrop(info: DropInfo) -> Bool {
        let result = performDropAction()
        dropTargetUUID = nil
        isDragging = false
        draggingUUIDs = []
        return result
    }

    func dropEnded(info: DropInfo) {
        dropTargetUUID = nil
        isDragging = false
        draggingUUIDs = []
    }
}

struct HierarchyPanel: View {
    @ObservedObject var editorState: EditorState
    @State private var searchText: String = ""
    @State private var showModelImporter: Bool = false
    @State private var expandedNodes: Set<String> = []
    @State private var dropTargetUUID: String? = nil
    @State private var draggingUUIDs: [String] = []
    @State private var isDragging: Bool = false

    private let entityDragType = UTType.plainText

    private let modelTypes: [UTType] = {
        let exts = [
            "fbx", "obj", "gltf", "glb", "dae", "blend", "3ds",
            "stl", "ply", "x", "smd", "md5mesh", "md2", "md3", "ms3d", "lwo", "lws"
        ]
        return exts.compactMap { UTType(filenameExtension: $0) }
    }()

    private var isSearching: Bool {
        !searchText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    private var filteredEntities: [EntityInfo] {
        if !isSearching {
            return editorState.entityList
        }

        return editorState.entityList.filter { entity in
            entity.name.localizedCaseInsensitiveContains(searchText)
        }
    }

    private var parentById: [String: String] {
        Dictionary(uniqueKeysWithValues: editorState.entityList.map { ($0.uuid, $0.parentUUID) })
    }

    private var hierarchyRoots: [HierarchyNode] {
        buildHierarchy(from: editorState.entityList)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header

            Divider()
                .overlay(EditorTheme.panelStroke)

            content
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
        .onAppear {
            syncExpandedNodes(with: editorState.entityList)
        }
        .onChange(of: editorState.entityList) { newValue in
            syncExpandedNodes(with: newValue)
        }
    }

    private var header: some View {
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
    }

    private var content: some View {
        VStack(alignment: .leading, spacing: 10) {
            searchBar
            hierarchyList
        }
        .padding(12)
    }

    private var searchBar: some View {
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
    }

    private var hierarchyList: some View {
        ZStack(alignment: .top) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 4) {
                    hierarchyRows
                }
                .padding(.vertical, 4)
            }

            if isDragging && dropTargetUUID == nil {
                rootDropHint
                    .padding(.top, 6)
            }
        }
        .onDrop(of: [entityDragType], delegate: rootDropDelegate())
    }

    @ViewBuilder
    private var hierarchyRows: some View {
        if isSearching {
            ForEach(filteredEntities) { entity in
                let node = HierarchyNode(entity: entity, children: [])
                HierarchyNodeView(
                    node: node,
                    depth: 0,
                    editorState: editorState,
                    expandedNodes: $expandedNodes,
                    dropTargetUUID: $dropTargetUUID,
                    draggingUUIDs: $draggingUUIDs,
                    isDragging: $isDragging,
                    entityDragType: entityDragType,
                    canReparent: { target in
                        canReparent(to: target)
                    },
                    reparentEntities: { target in
                        reparentEntities(to: target)
                    }
                )
            }
        } else {
            ForEach(hierarchyRoots) { node in
                HierarchyNodeView(
                    node: node,
                    depth: 0,
                    editorState: editorState,
                    expandedNodes: $expandedNodes,
                    dropTargetUUID: $dropTargetUUID,
                    draggingUUIDs: $draggingUUIDs,
                    isDragging: $isDragging,
                    entityDragType: entityDragType,
                    canReparent: { target in
                        canReparent(to: target)
                    },
                    reparentEntities: { target in
                        reparentEntities(to: target)
                    }
                )
            }
        }
    }

    private var rootDropHint: some View {
        HStack(spacing: 6) {
            Image(systemName: "tray.and.arrow.up")
                .font(EditorTheme.font(size: 11, weight: .semibold))
            Text("Drop to unparent")
                .font(EditorTheme.font(size: 11, weight: .semibold))
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color.accentColor.opacity(0.6), lineWidth: 1)
        )
        .cornerRadius(6)
        .foregroundColor(EditorTheme.textPrimary)
    }

    private func rootDropDelegate() -> HierarchyDropDelegate {
        HierarchyDropDelegate(
            targetUUID: nil,
            canDrop: { canReparent(to: nil) },
            performDropAction: { reparentEntities(to: nil) },
            dropTargetUUID: $dropTargetUUID,
            isDragging: $isDragging,
            draggingUUIDs: $draggingUUIDs
        )
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

    private func syncExpandedNodes(with entities: [EntityInfo]) {
        let validUUIDs = Set(entities.map { $0.uuid })
        if expandedNodes.isEmpty {
            expandedNodes = validUUIDs
        } else {
            expandedNodes = expandedNodes.intersection(validUUIDs)
        }
    }

    private func buildHierarchy(from entities: [EntityInfo]) -> [HierarchyNode] {
        let entityById = Dictionary(uniqueKeysWithValues: entities.map { ($0.uuid, $0) })
        var childrenMap: [String: [EntityInfo]] = [:]
        var roots: [EntityInfo] = []

        for entity in entities {
            let parentId = entity.parentUUID
            if !parentId.isEmpty, entityById[parentId] != nil {
                childrenMap[parentId, default: []].append(entity)
            } else {
                roots.append(entity)
            }
        }

        func buildNode(_ entity: EntityInfo) -> HierarchyNode {
            let children = (childrenMap[entity.uuid] ?? []).map(buildNode)
            return HierarchyNode(entity: entity, children: children)
        }

        return roots.map(buildNode)
    }

    private func canReparent(to targetUUID: String?) -> Bool {
        if draggingUUIDs.isEmpty {
            return false
        }
        if let targetUUID = targetUUID {
            if draggingUUIDs.contains(targetUUID) {
                return false
            }
            for dragged in draggingUUIDs {
                if isAncestor(dragged, of: targetUUID) {
                    return false
                }
            }
        }
        return true
    }

    private func reparentEntities(to targetUUID: String?) -> Bool {
        guard canReparent(to: targetUUID) else {
            return false
        }
        let parentUUID = targetUUID ?? ""
        var success = true
        for uuid in draggingUUIDs {
            let result = CrescentEngineBridge.shared().setEntityParent(child: uuid, parent: parentUUID)
            if !result {
                success = false
            }
        }
        editorState.refreshEntityList()
        return success
    }

    private func isAncestor(_ ancestorUUID: String, of candidateUUID: String) -> Bool {
        var current = parentById[candidateUUID] ?? ""
        while !current.isEmpty {
            if current == ancestorUUID {
                return true
            }
            current = parentById[current] ?? ""
        }
        return false
    }
}

private struct HierarchyNodeView: View {
    let node: HierarchyNode
    let depth: Int
    @ObservedObject var editorState: EditorState
    @Binding var expandedNodes: Set<String>
    @Binding var dropTargetUUID: String?
    @Binding var draggingUUIDs: [String]
    @Binding var isDragging: Bool
    let entityDragType: UTType
    let canReparent: (String?) -> Bool
    let reparentEntities: (String?) -> Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            rowView
            if hasChildren && isExpanded {
                ForEach(node.children) { child in
                    HierarchyNodeView(
                        node: child,
                        depth: depth + 1,
                        editorState: editorState,
                        expandedNodes: $expandedNodes,
                        dropTargetUUID: $dropTargetUUID,
                        draggingUUIDs: $draggingUUIDs,
                        isDragging: $isDragging,
                        entityDragType: entityDragType,
                        canReparent: canReparent,
                        reparentEntities: reparentEntities
                    )
                }
            }
        }
    }

    private var hasChildren: Bool {
        !node.children.isEmpty
    }

    private var isExpanded: Bool {
        expandedNodes.contains(node.entity.uuid)
    }

    private var isSelected: Bool {
        editorState.selectedEntityUUIDs.contains(node.entity.uuid)
    }

    private var isDropTarget: Bool {
        dropTargetUUID == node.entity.uuid
    }

    private var rowView: some View {
        HierarchyItem(
            name: node.entity.name,
            hasAnimator: node.entity.hasAnimator,
            hasSkinned: node.entity.hasSkinned,
            clipCount: node.entity.clipCount,
            depth: depth,
            hasChildren: hasChildren,
            isExpanded: isExpanded,
            isSelected: isSelected,
            isDropTarget: isDropTarget,
            onToggleExpand: {
                if isExpanded {
                    expandedNodes.remove(node.entity.uuid)
                } else {
                    expandedNodes.insert(node.entity.uuid)
                }
            },
            onSelect: { isToggle in
                editorState.selectEntity(node.entity.uuid, toggle: isToggle)
            },
            onBeginDrag: {
                beginDrag()
            }
        )
        .onDrop(of: [entityDragType], delegate: HierarchyDropDelegate(
            targetUUID: node.entity.uuid,
            canDrop: { canReparent(node.entity.uuid) },
            performDropAction: { reparentEntities(node.entity.uuid) },
            dropTargetUUID: $dropTargetUUID,
            isDragging: $isDragging,
            draggingUUIDs: $draggingUUIDs
        ))
    }

    private func beginDrag() -> NSItemProvider {
        let selection = editorState.selectedEntityUUIDs
        let dragSet = selection.contains(node.entity.uuid) ? Array(selection) : [node.entity.uuid]
        DispatchQueue.main.async {
            draggingUUIDs = dragSet
            isDragging = true
        }
        return NSItemProvider(object: node.entity.uuid as NSString)
    }
}

struct HierarchyItem: View {
    let name: String
    let hasAnimator: Bool
    let hasSkinned: Bool
    let clipCount: Int
    let depth: Int
    let hasChildren: Bool
    let isExpanded: Bool
    let isSelected: Bool
    let isDropTarget: Bool
    let onToggleExpand: () -> Void
    let onSelect: (_ isToggle: Bool) -> Void
    let onBeginDrag: () -> NSItemProvider

    var body: some View {
        HStack(spacing: 4) {
            if hasChildren {
                Button(action: onToggleExpand) {
                    Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                        .font(EditorTheme.font(size: 10, weight: .semibold))
                        .foregroundColor(EditorTheme.textMuted)
                        .frame(width: 14, height: 14)
                }
                .buttonStyle(.borderless)
            } else {
                Spacer()
                    .frame(width: 14, height: 14)
            }

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
        .padding(.leading, CGFloat(depth) * 14)
        .background(rowBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(rowStroke, lineWidth: 1)
        )
        .cornerRadius(6)
        .contentShape(Rectangle())
        .simultaneousGesture(
            TapGesture()
                .modifiers(.command)
                .onEnded { _ in
                    onSelect(true)
                }
        )
        .onTapGesture {
            onSelect(false)
        }
        .onDrag {
            onBeginDrag()
        }
    }

    private var rowBackground: Color {
        if isDropTarget {
            return Color.accentColor.opacity(0.18)
        }
        if isSelected {
            return Color.accentColor.opacity(0.2)
        }
        return Color.clear
    }

    private var rowStroke: Color {
        if isDropTarget {
            return Color.accentColor
        }
        if isSelected {
            return Color.accentColor
        }
        return EditorTheme.panelStroke
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
