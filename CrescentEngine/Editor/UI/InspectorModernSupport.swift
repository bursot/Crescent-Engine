import SwiftUI

struct InspectorSectionEntry: Identifiable {
    let id: String
    let view: AnyView
}

struct InspectorComponentChip: Identifiable {
    let id: String
    let title: String
    let icon: String
    let tint: Color
    let isActive: Bool
}

struct InspectorPanelContext {
    let editorState: EditorState
    let entity: EntityInfo
    let snapshot: InspectorEntitySnapshot

    var selectionCount: Int {
        editorState.selectedEntityUUIDs.count
    }
}

struct InspectorEntitySnapshot {
    let entityUUID: String
    let selectionCount: Int
    let hasLight: Bool
    let hasDecal: Bool
    let hasAnimation: Bool
    let hasAudioSource: Bool
    let hasRigidbody: Bool
    let hasCollider: Bool
    let hasHealth: Bool
    let hasCharacterController: Bool
    let hasFirstPersonController: Bool
    let hasThirdPersonController: Bool
    let hasBoneAttachment: Bool

    var hasPhysicsStack: Bool {
        hasRigidbody || hasCollider || hasHealth || hasCharacterController || hasFirstPersonController || hasThirdPersonController || hasBoneAttachment
    }

    var supportsMaterial: Bool {
        !hasLight && !hasDecal
    }

    var supportsPhysicsSection: Bool {
        !hasLight
    }

    var installedComponentCount: Int {
        var count = 1 // Transform
        if supportsMaterial { count += 1 }
        if hasPhysicsStack { count += 1 }
        if hasAudioSource { count += 1 }
        if hasAnimation { count += 1 }
        if hasDecal { count += 1 }
        if hasLight { count += 1 }
        return count
    }

    var componentChips: [InspectorComponentChip] {
        var chips: [InspectorComponentChip] = [
            InspectorComponentChip(
                id: "transform",
                title: "Transform",
                icon: "move.3d",
                tint: EditorTheme.textAccent,
                isActive: true
            )
        ]

        if supportsMaterial {
            chips.append(
                InspectorComponentChip(
                    id: "material",
                    title: "Material",
                    icon: "paintpalette.fill",
                    tint: EditorTheme.accentSecondary,
                    isActive: true
                )
            )
        }

        if supportsPhysicsSection {
            chips.append(
                InspectorComponentChip(
                    id: "physics",
                    title: "Physics",
                    icon: "atom",
                    tint: Color.orange,
                    isActive: hasPhysicsStack
                )
            )
        }

        chips.append(
            InspectorComponentChip(
                id: "audio",
                title: "Audio",
                icon: "speaker.wave.2.fill",
                tint: Color.teal,
                isActive: hasAudioSource
            )
        )

        if hasAnimation {
            chips.append(
                InspectorComponentChip(
                    id: "animation",
                    title: "Animation",
                    icon: "figure.walk",
                    tint: Color.pink,
                    isActive: true
                )
            )
        }

        if hasDecal {
            chips.append(
                InspectorComponentChip(
                    id: "decal",
                    title: "Decal",
                    icon: "square.on.square",
                    tint: Color.indigo,
                    isActive: true
                )
            )
        }

        if hasLight {
            chips.append(
                InspectorComponentChip(
                    id: "light",
                    title: "Light",
                    icon: "lightbulb.fill",
                    tint: Color.yellow,
                    isActive: true
                )
            )
        }

        if selectionCount > 1 {
            chips.append(
                InspectorComponentChip(
                    id: "hlod",
                    title: "HLOD",
                    icon: "square.stack.3d.up",
                    tint: Color.cyan,
                    isActive: true
                )
            )
        }

        return chips
    }

    static func make(for entity: EntityInfo, selectionCount: Int, bridge: CrescentEngineBridge = CrescentEngineBridge.shared()) -> InspectorEntitySnapshot {
        InspectorEntitySnapshot(
            entityUUID: entity.uuid,
            selectionCount: selectionCount,
            hasLight: bridge.hasInspectorPayload(bridge.getLightInfo(entity.uuid)),
            hasDecal: bridge.hasInspectorPayload(bridge.getDecalInfo(entity.uuid)),
            hasAnimation: bridge.hasInspectorPayload(bridge.getSkinnedMeshInfo(uuid: entity.uuid)),
            hasAudioSource: bridge.hasInspectorPayload(bridge.getAudioSourceInfo(uuid: entity.uuid)),
            hasRigidbody: bridge.hasInspectorPayload(bridge.getRigidbodyInfo(uuid: entity.uuid)),
            hasCollider: bridge.hasInspectorPayload(bridge.getColliderInfo(uuid: entity.uuid)),
            hasHealth: bridge.hasInspectorPayload(bridge.getHealthInfo(uuid: entity.uuid)),
            hasCharacterController: bridge.hasInspectorPayload(bridge.getCharacterControllerInfo(uuid: entity.uuid)),
            hasFirstPersonController: bridge.hasInspectorPayload(bridge.getFirstPersonControllerInfo(uuid: entity.uuid)),
            hasThirdPersonController: bridge.hasInspectorPayload(bridge.getThirdPersonControllerInfo(uuid: entity.uuid)),
            hasBoneAttachment: bridge.hasInspectorPayload(bridge.getBoneAttachmentInfo(uuid: entity.uuid))
        )
    }
}

enum InspectorQuickAddAction: String, CaseIterable, Identifiable {
    case rigidbody
    case collider
    case health
    case characterController
    case firstPersonController
    case thirdPersonController
    case boneAttachment
    case audioSource

    var id: String { rawValue }

    var title: String {
        switch self {
        case .rigidbody: return "Rigidbody"
        case .collider: return "Collider"
        case .health: return "Health"
        case .characterController: return "Character Controller"
        case .firstPersonController: return "First Person Controller"
        case .thirdPersonController: return "Third Person Controller"
        case .boneAttachment: return "Bone Attachment"
        case .audioSource: return "Audio Source"
        }
    }

    var subtitle: String {
        switch self {
        case .rigidbody: return "Dynamic, kinematic or static body"
        case .collider: return "Shape, trigger and collision settings"
        case .health: return "Simple health and death handling"
        case .characterController: return "Grounded movement and jumping"
        case .firstPersonController: return "Mouse look and FPS controls"
        case .thirdPersonController: return "Orbit camera and 3P locomotion"
        case .boneAttachment: return "Attach props to animated skeleton bones"
        case .audioSource: return "3D or 2D playback source"
        }
    }

    var icon: String {
        switch self {
        case .rigidbody: return "circle.hexagongrid.fill"
        case .collider: return "square.dashed"
        case .health: return "heart.text.square.fill"
        case .characterController: return "figure.stand"
        case .firstPersonController: return "scope"
        case .thirdPersonController: return "figure.run"
        case .boneAttachment: return "link.circle.fill"
        case .audioSource: return "speaker.wave.2.fill"
        }
    }

    var tint: Color {
        switch self {
        case .rigidbody, .collider, .characterController, .firstPersonController, .thirdPersonController, .boneAttachment:
            return Color.orange
        case .health:
            return Color.red
        case .audioSource:
            return Color.teal
        }
    }

    func isSupported(in snapshot: InspectorEntitySnapshot) -> Bool {
        switch self {
        case .audioSource:
            return true
        default:
            return snapshot.supportsPhysicsSection
        }
    }

    func isInstalled(in snapshot: InspectorEntitySnapshot) -> Bool {
        switch self {
        case .rigidbody: return snapshot.hasRigidbody
        case .collider: return snapshot.hasCollider
        case .health: return snapshot.hasHealth
        case .characterController: return snapshot.hasCharacterController
        case .firstPersonController: return snapshot.hasFirstPersonController
        case .thirdPersonController: return snapshot.hasThirdPersonController
        case .boneAttachment: return snapshot.hasBoneAttachment
        case .audioSource: return snapshot.hasAudioSource
        }
    }

    @discardableResult
    func install(on entityUUID: String, bridge: CrescentEngineBridge = CrescentEngineBridge.shared()) -> Bool {
        switch self {
        case .rigidbody:
            return bridge.addRigidbody(uuid: entityUUID)
        case .collider:
            return bridge.addCollider(uuid: entityUUID)
        case .health:
            return bridge.addHealth(uuid: entityUUID)
        case .characterController:
            return bridge.addCharacterController(uuid: entityUUID)
        case .firstPersonController:
            return bridge.addFirstPersonController(uuid: entityUUID)
        case .thirdPersonController:
            return bridge.addThirdPersonController(uuid: entityUUID)
        case .boneAttachment:
            return bridge.addBoneAttachment(uuid: entityUUID)
        case .audioSource:
            return bridge.addAudioSource(uuid: entityUUID)
        }
    }
}

enum InspectorSectionRegistry {
    static func makeSections(for context: InspectorPanelContext) -> [InspectorSectionEntry] {
        let selectedUUIDs = context.editorState.selectedEntityUUIDs
        let entityUUID = context.entity.uuid
        let editorState = context.editorState

        var sections: [InspectorSectionEntry] = [
            InspectorSectionEntry(
                id: "transform",
                view: AnyView(
                    ComponentSection(
                        title: "Transform",
                        icon: "move.3d",
                        subtitle: "Local position, rotation and scale",
                        accent: EditorTheme.textAccent
                    ) {
                        TransformInspector(selectedUUIDs: selectedUUIDs)
                    }
                )
            )
        ]

        if context.snapshot.supportsMaterial {
            sections.append(
                InspectorSectionEntry(
                    id: "material",
                    view: AnyView(
                        ComponentSection(
                            title: "Material",
                            icon: "paintpalette.fill",
                            subtitle: "Surface textures, shading and slots",
                            accent: EditorTheme.accentSecondary
                        ) {
                            MaterialInspector(
                                entityUUID: entityUUID,
                                selectedUUIDs: selectedUUIDs,
                                editorState: editorState
                            )
                        }
                    )
                )
            )
        }

        if context.selectionCount > 1 {
            sections.append(
                InspectorSectionEntry(
                    id: "hlod",
                    view: AnyView(
                        ComponentSection(
                            title: "HLOD",
                            icon: "square.stack.3d.up",
                            subtitle: "Batch operations for the current selection",
                            accent: Color.cyan
                        ) {
                            HLODInspectorSection(uuids: Array(selectedUUIDs))
                        }
                    )
                )
            )
        }

        if context.snapshot.supportsPhysicsSection {
            sections.append(
                InspectorSectionEntry(
                    id: "physics",
                    view: AnyView(
                        ComponentSection(
                            title: "Physics",
                            icon: "atom",
                            subtitle: "Bodies, colliders and gameplay control",
                            accent: Color.orange,
                            badge: context.snapshot.hasPhysicsStack ? "Configured" : "Available"
                        ) {
                            PhysicsInspector(entityUUID: entityUUID)
                        }
                    )
                )
            )
        }

        sections.append(
            InspectorSectionEntry(
                id: "audio",
                view: AnyView(
                    ComponentSection(
                        title: "Audio",
                        icon: "speaker.wave.2.fill",
                        subtitle: "Playback, attenuation and source settings",
                        accent: Color.teal,
                        badge: context.snapshot.hasAudioSource ? "Mounted" : "Empty"
                    ) {
                        AudioInspector(entityUUID: entityUUID)
                    }
                )
            )
        )

        if context.snapshot.hasAnimation {
            sections.append(
                InspectorSectionEntry(
                    id: "animation",
                    view: AnyView(
                        ComponentSection(
                            title: "Animation",
                            icon: "figure.walk",
                            subtitle: "Clips, states, parameters and events",
                            accent: Color.pink
                        ) {
                            AnimationInspector(entityUUID: entityUUID)
                        }
                    )
                )
            )
        }

        if context.snapshot.hasDecal {
            sections.append(
                InspectorSectionEntry(
                    id: "decal",
                    view: AnyView(
                        ComponentSection(
                            title: "Decal",
                            icon: "square.on.square",
                            subtitle: "Projection, textures and UV controls",
                            accent: Color.indigo
                        ) {
                            DecalInspector(entityUUID: entityUUID)
                        }
                    )
                )
            )
        }

        if context.snapshot.hasLight {
            sections.append(
                InspectorSectionEntry(
                    id: "light",
                    view: AnyView(
                        ComponentSection(
                            title: "Light",
                            icon: "lightbulb.fill",
                            subtitle: "Emission, shadows and falloff",
                            accent: Color.yellow
                        ) {
                            LightInspector(uuid: entityUUID)
                        }
                    )
                )
            )
        }

        return sections
    }
}

struct InspectorOverviewCard: View {
    let context: InspectorPanelContext
    let onRename: (String) -> Void
    let onRefresh: () -> Void

    @State private var nameDraft: String

    init(context: InspectorPanelContext, onRename: @escaping (String) -> Void, onRefresh: @escaping () -> Void) {
        self.context = context
        self.onRename = onRename
        self.onRefresh = onRefresh
        _nameDraft = State(initialValue: context.entity.name)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .top, spacing: 12) {
                ZStack {
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .fill(
                            LinearGradient(
                                colors: [
                                    EditorTheme.textAccent.opacity(0.32),
                                    EditorTheme.accentSecondary.opacity(0.18)
                                ],
                                startPoint: .topLeading,
                                endPoint: .bottomTrailing
                            )
                        )
                    Image(systemName: context.selectionCount > 1 ? "square.stack.3d.up.fill" : "cube.fill")
                        .font(EditorTheme.font(size: 18, weight: .semibold))
                        .foregroundColor(EditorTheme.textPrimary)
                }
                .frame(width: 48, height: 48)

                VStack(alignment: .leading, spacing: 6) {
                    if context.selectionCount > 1 {
                        Text("\(context.selectionCount) objects selected")
                            .font(EditorTheme.font(size: 15, weight: .semibold))
                            .foregroundColor(EditorTheme.textPrimary)

                        Text("Batch editing uses the primary selection as the reference object.")
                            .font(EditorTheme.font(size: 11))
                            .foregroundColor(EditorTheme.textMuted)
                    } else {
                        TextField("Entity Name", text: $nameDraft)
                            .textFieldStyle(.plain)
                            .font(EditorTheme.font(size: 15, weight: .semibold))
                            .foregroundColor(EditorTheme.textPrimary)
                            .onSubmit {
                                commitRename()
                            }

                        Text("Component-driven entity inspector")
                            .font(EditorTheme.font(size: 11))
                            .foregroundColor(EditorTheme.textMuted)
                    }
                }

                Spacer(minLength: 12)

                if context.selectionCount == 1 {
                    InspectorQuickAddMenu(snapshot: context.snapshot, entityUUID: context.entity.uuid) {
                        onRefresh()
                    }
                }
            }

            HStack(spacing: 8) {
                InspectorMetaBadge(
                    title: context.selectionCount > 1 ? "Selection" : "Entity",
                    value: context.selectionCount > 1 ? "\(context.selectionCount)" : shortUUID(context.entity.uuid),
                    tint: EditorTheme.textAccent
                )

                InspectorMetaBadge(
                    title: "Sections",
                    value: "\(context.snapshot.installedComponentCount)",
                    tint: EditorTheme.accentSecondary
                )

                if context.snapshot.hasPhysicsStack {
                    InspectorMetaBadge(title: "Gameplay", value: "Physics", tint: Color.orange)
                }

                if context.snapshot.hasAudioSource {
                    InspectorMetaBadge(title: "Sound", value: "Ready", tint: Color.teal)
                }
            }

            if !context.snapshot.componentChips.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        ForEach(context.snapshot.componentChips) { chip in
                            InspectorComponentPill(chip: chip)
                        }
                    }
                    .padding(.vertical, 2)
                }
            }
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            LinearGradient(
                colors: [
                    EditorTheme.surfaceElevated.opacity(0.92),
                    EditorTheme.surface.opacity(0.98)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
        )
        .overlay(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .stroke(EditorTheme.panelStrokeStrong, lineWidth: 1)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .inset(by: 1)
                .stroke(EditorTheme.panelStroke.opacity(0.7), lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        .shadow(color: EditorTheme.panelShadow.opacity(0.7), radius: 16, x: 0, y: 10)
        .onChange(of: context.entity.name) { newValue in
            nameDraft = newValue
        }
    }

    private func shortUUID(_ uuid: String) -> String {
        String(uuid.prefix(8)).uppercased()
    }

    private func commitRename() {
        let trimmed = nameDraft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            nameDraft = context.entity.name
            return
        }
        if trimmed != context.entity.name {
            onRename(trimmed)
        }
    }
}

struct InspectorQuickAddMenu: View {
    let snapshot: InspectorEntitySnapshot
    let entityUUID: String
    let onApplied: () -> Void

    private var supportedActions: [InspectorQuickAddAction] {
        InspectorQuickAddAction.allCases.filter { $0.isSupported(in: snapshot) }
    }

    var body: some View {
        Menu {
            if supportedActions.isEmpty {
                Text("No compatible components")
            } else {
                ForEach(supportedActions) { action in
                    Button {
                        if action.install(on: entityUUID) {
                            onApplied()
                        }
                    } label: {
                        Label(action.title, systemImage: action.isInstalled(in: snapshot) ? "checkmark.circle.fill" : action.icon)
                    }
                    .disabled(action.isInstalled(in: snapshot))
                }
            }
        } label: {
            HStack(spacing: 8) {
                Image(systemName: "plus.circle.fill")
                    .font(EditorTheme.font(size: 12, weight: .semibold))
                Text("Add Component")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
            }
            .foregroundColor(EditorTheme.textPrimary)
            .padding(.horizontal, 12)
            .padding(.vertical, 9)
            .background(
                Capsule(style: .continuous)
                    .fill(
                        LinearGradient(
                            colors: [EditorTheme.textAccent.opacity(0.28), EditorTheme.accentSecondary.opacity(0.18)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(EditorTheme.panelStrokeStrong, lineWidth: 1)
            )
        }
        .menuStyle(.borderlessButton)
    }
}

struct InspectorMetaBadge: View {
    let title: String
    let value: String
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title.uppercased())
                .font(EditorTheme.font(size: 9, weight: .semibold))
                .foregroundColor(EditorTheme.textMuted)
            Text(value)
                .font(EditorTheme.mono(size: 10))
                .foregroundColor(EditorTheme.textPrimary)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(tint.opacity(0.10))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(tint.opacity(0.25), lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }
}

struct InspectorComponentPill: View {
    let chip: InspectorComponentChip

    var body: some View {
        HStack(spacing: 7) {
            Image(systemName: chip.icon)
                .font(EditorTheme.font(size: 10, weight: .semibold))
            Text(chip.title)
                .font(EditorTheme.font(size: 10, weight: .medium))
            Circle()
                .fill(chip.isActive ? chip.tint : EditorTheme.textMuted.opacity(0.35))
                .frame(width: 6, height: 6)
        }
        .foregroundColor(chip.isActive ? EditorTheme.textPrimary : EditorTheme.textMuted)
        .padding(.horizontal, 10)
        .padding(.vertical, 7)
        .background(
            Capsule(style: .continuous)
                .fill(chip.tint.opacity(chip.isActive ? 0.16 : 0.08))
        )
        .overlay(
            Capsule(style: .continuous)
                .stroke(chip.tint.opacity(chip.isActive ? 0.32 : 0.12), lineWidth: 1)
        )
    }
}

struct InspectorEmptyState: View {
    var body: some View {
        VStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 22, style: .continuous)
                    .fill(EditorTheme.surfaceElevated.opacity(0.55))
                    .frame(width: 92, height: 92)

                Image(systemName: "cursorarrow.rays")
                    .font(EditorTheme.font(size: 30, weight: .semibold))
                    .foregroundColor(EditorTheme.textAccent)
            }

            VStack(spacing: 6) {
                Text("No Selection")
                    .font(EditorTheme.font(size: 15, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)

                Text("Pick an entity from the Hierarchy to inspect components, materials and gameplay data.")
                    .font(EditorTheme.font(size: 11))
                    .foregroundColor(EditorTheme.textMuted)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 240)
            }
        }
        .frame(maxWidth: .infinity, minHeight: 240)
        .padding(24)
        .background(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .fill(EditorTheme.surface.opacity(0.72))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .stroke(
                    EditorTheme.panelStrokeStrong.opacity(0.7),
                    style: StrokeStyle(lineWidth: 1, dash: [6, 6])
                )
        )
    }
}

private extension CrescentEngineBridge {
    func hasInspectorPayload(_ value: Any?) -> Bool {
        guard let dictionary = value as? [String: Any] else {
            return false
        }
        return !dictionary.isEmpty
    }
}
