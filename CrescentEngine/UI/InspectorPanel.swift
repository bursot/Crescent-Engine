import SwiftUI
import Combine
import UniformTypeIdentifiers
import AppKit

struct InspectorPanel: View {
    @ObservedObject var editorState: EditorState
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Label("Inspector", systemImage: "cube.transparent")
                    .labelStyle(.titleAndIcon)
                    .font(EditorTheme.font(size: 12, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                
                Spacer()
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(EditorTheme.panelHeader)
            
            Divider()
                .overlay(EditorTheme.panelStroke)
            
            ScrollView {
                // Get selected entity name from UUID
                if let selectedUUID = editorState.primarySelectionUUID,
                   let selectedEntity = editorState.entityList.first(where: { $0.uuid == selectedUUID }) {
                    let lightSection = LightInspectorSection(uuid: selectedEntity.uuid)
                    let animationSection = AnimationInspectorSection(uuid: selectedEntity.uuid)
                    let decalSection = DecalInspectorSection(uuid: selectedEntity.uuid)
                    let physicsSection = PhysicsInspectorSection(uuid: selectedEntity.uuid)
                    let audioSection = AudioInspectorSection(uuid: selectedEntity.uuid)
                    VStack(alignment: .leading, spacing: 14) {
                        InspectorSummaryCard(
                            entityUUID: selectedEntity.uuid,
                            entityName: selectedEntity.name,
                            selectionCount: editorState.selectedEntityUUIDs.count
                        ) { newName in
                            let _ = CrescentEngineBridge.shared().setEntityName(uuid: selectedEntity.uuid, name: newName)
                            editorState.refreshEntityList()
                        }
                        
                        ComponentSection(title: "Transform", icon: "arrow.up.and.down.and.arrow.left.and.right") {
                            TransformInspector(selectedUUIDs: editorState.selectedEntityUUIDs)  // Pass ALL selected UUIDs
                        }
                        
                        // Only show Material if this entity isn't a light
                        if lightSection == nil && decalSection == nil {
                            ComponentSection(title: "Material", icon: "paintpalette.fill") {
                                MaterialInspector(entityUUID: selectedEntity.uuid,
                                                  selectedUUIDs: editorState.selectedEntityUUIDs)
                            }
                        }

                        if editorState.selectedEntityUUIDs.count > 1 {
                            ComponentSection(title: "HLOD", icon: "square.stack.3d.up") {
                                HLODInspectorSection(uuids: Array(editorState.selectedEntityUUIDs))
                            }
                        }

                        if let physicsView = physicsSection {
                            physicsView
                        }

                        if let audioView = audioSection {
                            audioView
                        }
                        
                        if let animationView = animationSection {
                            animationView
                        }
                        
                        if let decalView = decalSection {
                            decalView
                        }
                        
                        if let lightView = lightSection {
                            lightView
                        }
                        
                    }
                    .padding(12)
                } else {
                    InspectorPlaceholder()
                        .padding(12)
                }
            }
        }
        .frame(minWidth: 250, maxWidth: 380)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }
}

struct ComponentSection<Content: View>: View {
    let title: String
    let icon: String
    @ViewBuilder let content: Content
    @State private var isExpanded: Bool = true
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Button {
                withAnimation(.easeInOut(duration: 0.2)) {
                    isExpanded.toggle()
                }
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: icon)
                        .font(EditorTheme.font(size: 12))
                        .foregroundColor(.accentColor)
                        .frame(width: 16)
                    
                    Text(title)
                        .font(EditorTheme.font(size: 12, weight: .semibold))
                    
                    Spacer()
                    
                    Image(systemName: "chevron.right")
                        .font(EditorTheme.font(size: 10, weight: .semibold))
                        .rotationEffect(.degrees(isExpanded ? 90 : 0))
                        .foregroundColor(EditorTheme.textMuted)
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
            }
            .buttonStyle(.plain)
            
            if isExpanded {
                Divider()
                    .overlay(EditorTheme.panelStroke)
                content
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}

struct TransformInspector: View {
    let selectedUUIDs: Set<String>  // All selected UUIDs
    @State private var position: [Float] = [0, 0, 0]
    @State private var rotation: [Float] = [0, 0, 0]
    @State private var scale: [Float] = [1, 1, 1]
    
    let timer = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()
    
    var body: some View {
        VStack(spacing: 12) {
            // Position
            VectorField(label: "Position", 
                       values: $position,
                       color: .red,
                       selectedUUIDs: selectedUUIDs,
                       transformType: .position)
            
            // Rotation  
            VectorField(label: "Rotation", 
                       values: $rotation,
                       color: .green,
                       selectedUUIDs: selectedUUIDs,
                       transformType: .rotation)
            
            // Scale
            VectorField(label: "Scale", 
                       values: $scale,
                       color: .blue,
                       selectedUUIDs: selectedUUIDs,
                       transformType: .scale)
        }
        .onAppear {
            refreshTransform()
        }
        .onReceive(timer) { _ in
            refreshTransform()
        }
    }
    
    func refreshTransform() {
        // Read from first selected entity (for display)
        guard let firstUUID = selectedUUIDs.first else { return }
        let bridge = CrescentEngineBridge.shared()
        
        if let pos = bridge.getPosition(uuid: firstUUID) as? [NSNumber] {
            position = pos.map { $0.floatValue }
        }
        
        if let rot = bridge.getRotation(uuid: firstUUID) as? [NSNumber], rot.count >= 3 {
            let newRot = rot.map { $0.floatValue }
            rotation = [
                stabilizeAngle(newRot[0], rotation[safe: 0] ?? newRot[0]),
                stabilizeAngle(newRot[1], rotation[safe: 1] ?? newRot[1]),
                stabilizeAngle(newRot[2], rotation[safe: 2] ?? newRot[2])
            ]
        }
        
        if let scl = bridge.getScale(uuid: firstUUID) as? [NSNumber] {
            scale = scl.map { $0.floatValue }
        }
    }

    private func stabilizeAngle(_ value: Float, _ current: Float) -> Float {
        var result = value
        while result - current > 180.0 { result -= 360.0 }
        while result - current < -180.0 { result += 360.0 }
        return result
    }
}

// Helper to show light inspector if applicable
func LightInspectorSection(uuid: String) -> AnyView? {
    let bridge = CrescentEngineBridge.shared()
    if let info = bridge.getLightInfo(uuid) as? [String: Any], !info.isEmpty {
        return AnyView(
            ComponentSection(title: "Light", icon: "lightbulb") {
                LightInspector(uuid: uuid)
            }
        )
    }
    return nil
}

func AnimationInspectorSection(uuid: String) -> AnyView? {
    let bridge = CrescentEngineBridge.shared()
    if let info = bridge.getSkinnedMeshInfo(uuid: uuid) as? [String: Any], !info.isEmpty {
        return AnyView(
            ComponentSection(title: "Animation", icon: "figure.walk") {
                AnimationInspector(entityUUID: uuid)
            }
        )
    }
    return nil
}

func PhysicsInspectorSection(uuid: String) -> AnyView? {
    let bridge = CrescentEngineBridge.shared()
    if let lightInfo = bridge.getLightInfo(uuid) as? [String: Any], !lightInfo.isEmpty {
        return nil
    }
    return AnyView(
        ComponentSection(title: "Physics", icon: "atom") {
            PhysicsInspector(entityUUID: uuid)
        }
    )
}

func AudioInspectorSection(uuid: String) -> AnyView? {
    return AnyView(
        ComponentSection(title: "Audio", icon: "speaker.wave.2.fill") {
            AudioInspector(entityUUID: uuid)
        }
    )
}

func DecalInspectorSection(uuid: String) -> AnyView? {
    let bridge = CrescentEngineBridge.shared()
    if let info = bridge.getDecalInfo(uuid) as? [String: Any], !info.isEmpty {
        return AnyView(
            ComponentSection(title: "Decal", icon: "square.on.square") {
                DecalInspector(entityUUID: uuid)
            }
        )
    }
    return nil
}

struct InspectorSummaryCard: View {
    let entityUUID: String
    let entityName: String
    let selectionCount: Int
    let onRename: (String) -> Void

    @State private var nameDraft: String

    init(entityUUID: String, entityName: String, selectionCount: Int, onRename: @escaping (String) -> Void) {
        self.entityUUID = entityUUID
        self.entityName = entityName
        self.selectionCount = selectionCount
        self.onRename = onRename
        _nameDraft = State(initialValue: entityName)
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            if selectionCount > 1 {
                Text("\(selectionCount) objects selected")
                    .font(EditorTheme.font(size: 12, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
            } else {
                TextField("Name", text: $nameDraft)
                    .textFieldStyle(.roundedBorder)
                    .font(EditorTheme.font(size: 13, weight: .semibold))
                    .onSubmit {
                        commitRename()
                    }
            }
            
            Text(selectionCount > 1 ? "Multiple Selection" : "GameObject")
                .font(EditorTheme.font(size: 11))
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .onChange(of: entityName) { newValue in
            nameDraft = newValue
        }
    }

    private func commitRename() {
        let trimmed = nameDraft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            nameDraft = entityName
            return
        }
        if trimmed != entityName {
            onRename(trimmed)
        }
    }
}

struct InspectorPlaceholder: View {
    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "cube.transparent")
                .font(EditorTheme.font(size: 44, weight: .semibold))
                .foregroundColor(EditorTheme.textMuted)
            
            Text("No Selection")
                .font(EditorTheme.font(size: 14, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)
            
            Text("Select an object in the Hierarchy")
                .font(EditorTheme.font(size: 11))
                .foregroundColor(EditorTheme.textMuted)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding()
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }
}

struct AnimationInspector: View {
    let entityUUID: String
    @Environment(\.openWindow) private var openWindow

    private struct AnimatorParam: Identifiable {
        let id: String
        var name: String
        var type: String
        var floatValue: Float
        var intValue: Int
        var boolValue: Bool
    }

    private struct AnimationEventItem: Identifiable {
        let id = UUID()
        var name: String
        var time: Float
    }
    
    @State private var clipNames: [String] = []
    @State private var clipIndex: Int = 0
    @State private var stateNames: [String] = []
    @State private var stateIndex: Int = 0
    @State private var blendDuration: Float = 0.25
    @State private var playing: Bool = false
    @State private var looping: Bool = true
    @State private var speed: Float = 1.0
    @State private var time: Float = 0.0
    @State private var duration: Float = 0.0
    @State private var rootMotionEnabled: Bool = false
    @State private var rootMotionPosition: Bool = true
    @State private var rootMotionRotation: Bool = false
    @State private var parameters: [AnimatorParam] = []
    @State private var clipEvents: [AnimationEventItem] = []
    @State private var newEventName: String = ""
    @State private var newEventTime: Float = 0.0
    @State private var eventLog: [AnimationEventItem] = []
    
    private let timer = Timer.publish(every: 0.6, on: .main, in: .common).autoconnect()
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if clipNames.isEmpty && stateNames.isEmpty {
                Text("No animation clips")
                    .font(EditorTheme.font(size: 11))
                    .foregroundColor(EditorTheme.textMuted)
            } else {
                if !stateNames.isEmpty {
                    Picker("State", selection: Binding(
                        get: { stateIndex },
                        set: { newVal in
                            stateIndex = newVal
                            push(["stateIndex": newVal, "blendDuration": blendDuration])
                        })) {
                        ForEach(0..<stateNames.count, id: \.self) { idx in
                            Text(stateNames[idx]).tag(idx)
                        }
                    }
                    .pickerStyle(.menu)
                    .font(EditorTheme.font(size: 11, weight: .medium))
                    
                    SliderRow(title: "Blend", value: $blendDuration, range: 0...1.5, step: 0.01) { newVal in
                        push(["blendDuration": newVal])
                    }
                } else {
                    if clipNames.count > 1 {
                        Picker("Clip", selection: Binding(
                            get: { clipIndex },
                            set: { newVal in
                                clipIndex = newVal
                                push(["clipIndex": newVal])
                            })) {
                            ForEach(0..<clipNames.count, id: \.self) { idx in
                                Text(clipNames[idx]).tag(idx)
                            }
                        }
                        .pickerStyle(.menu)
                        .font(EditorTheme.font(size: 11, weight: .medium))
                    } else if let name = clipNames.first {
                        Text("Clip: \(name)")
                            .font(EditorTheme.font(size: 11, weight: .medium))
                            .foregroundColor(EditorTheme.textPrimary)
                    }
                }

                HStack(spacing: 8) {
                    Button("Graph") {
                        openWindow(id: "animGraph")
                    }
                    .buttonStyle(.bordered)

                    Button("Sequence") {
                        openWindow(id: "animSequence")
                    }
                    .buttonStyle(.bordered)

                    Button("Skeleton") {
                        openWindow(id: "skeleton")
                    }
                    .buttonStyle(.bordered)
                }
                .font(EditorTheme.font(size: 11, weight: .semibold))
                
                Toggle("Play", isOn: Binding(
                    get: { playing },
                    set: { newVal in
                        playing = newVal
                        push(["playing": newVal])
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))
                
                Toggle("Loop", isOn: Binding(
                    get: { looping },
                    set: { newVal in
                        looping = newVal
                        push(["looping": newVal])
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))
                
                SliderRow(title: "Speed", value: $speed, range: 0...3, step: 0.05) { newVal in
                    push(["speed": newVal])
                }
                
                if duration > 0.0 {
                    SliderRow(title: "Time", value: $time, range: 0...duration, step: 0.01) { newVal in
                        push(["time": newVal])
                    }
                    
                    HStack {
                        Text(String(format: "%.2fs / %.2fs", time, duration))
                            .font(EditorTheme.mono(size: 10))
                            .foregroundColor(EditorTheme.textMuted)
                        Spacer()
                        Button("Restart") {
                            time = 0.0
                            push(["time": 0.0])
                        }
                        .buttonStyle(.borderless)
                    }
                }

                Divider()
                    .overlay(EditorTheme.panelStroke)

                VStack(alignment: .leading, spacing: 6) {
                    Text("Root Motion")
                        .font(EditorTheme.font(size: 11, weight: .semibold))
                        .foregroundColor(EditorTheme.textPrimary)

                    Toggle("Enabled", isOn: Binding(
                        get: { rootMotionEnabled },
                        set: { newVal in
                            rootMotionEnabled = newVal
                            updateRootMotion()
                        }))
                    .font(EditorTheme.font(size: 11, weight: .medium))

                    if rootMotionEnabled {
                        Toggle("Apply Position", isOn: Binding(
                            get: { rootMotionPosition },
                            set: { newVal in
                                rootMotionPosition = newVal
                                updateRootMotion()
                            }))
                        .font(EditorTheme.font(size: 11, weight: .medium))

                        Toggle("Apply Rotation", isOn: Binding(
                            get: { rootMotionRotation },
                            set: { newVal in
                                rootMotionRotation = newVal
                                updateRootMotion()
                            }))
                        .font(EditorTheme.font(size: 11, weight: .medium))
                    }
                }

                Divider()
                    .overlay(EditorTheme.panelStroke)

                VStack(alignment: .leading, spacing: 8) {
                    Text("Parameters")
                        .font(EditorTheme.font(size: 11, weight: .semibold))
                        .foregroundColor(EditorTheme.textPrimary)

                    if parameters.isEmpty {
                        Text("No parameters yet. Use Open Graph Editor to add them.")
                            .font(EditorTheme.font(size: 10))
                            .foregroundColor(EditorTheme.textMuted)
                    } else {
                        ForEach(parameters.indices, id: \.self) { idx in
                            let param = parameters[idx]
                            switch param.type {
                            case "Bool":
                                Toggle(param.name, isOn: Binding(
                                    get: { parameters[idx].boolValue },
                                    set: { newVal in
                                        parameters[idx].boolValue = newVal
                                        setAnimatorParameter(param.name, type: param.type, value: newVal)
                                    }))
                                .font(EditorTheme.font(size: 11, weight: .medium))
                            case "Int":
                                Stepper(param.name, value: Binding(
                                    get: { parameters[idx].intValue },
                                    set: { newVal in
                                        parameters[idx].intValue = newVal
                                        setAnimatorParameter(param.name, type: param.type, value: newVal)
                                    }), in: -100...100)
                                .font(EditorTheme.font(size: 11, weight: .medium))
                            case "Trigger":
                                Button("Fire \(param.name)") {
                                    CrescentEngineBridge.shared().fireAnimatorTrigger(uuid: entityUUID, name: param.name)
                                }
                                .buttonStyle(.borderless)
                            default:
                                SliderRow(title: param.name, value: Binding(
                                    get: { parameters[idx].floatValue },
                                    set: { newVal in
                                        parameters[idx].floatValue = newVal
                                        setAnimatorParameter(param.name, type: param.type, value: newVal)
                                    }), range: -5...5, step: 0.01, tint: .blue, onChange: { _ in })
                            }
                        }
                    }
                }

                Divider()
                    .overlay(EditorTheme.panelStroke)

                IKInspector(entityUUID: entityUUID)

                Divider()
                    .overlay(EditorTheme.panelStroke)

                VStack(alignment: .leading, spacing: 8) {
                    Text("Events")
                        .font(EditorTheme.font(size: 11, weight: .semibold))
                        .foregroundColor(EditorTheme.textPrimary)

                    if clipEvents.isEmpty {
                        Text("No events on this clip")
                            .font(EditorTheme.font(size: 10))
                            .foregroundColor(EditorTheme.textMuted)
                    } else {
                        ForEach(clipEvents.indices, id: \.self) { idx in
                            HStack(spacing: 8) {
                                TextField("Name", text: Binding(
                                    get: { clipEvents[idx].name },
                                    set: { newVal in
                                        clipEvents[idx].name = newVal
                                        commitEvents()
                                    }))
                                .textFieldStyle(.roundedBorder)
                                .font(EditorTheme.font(size: 10))

                                TextField("Time", value: Binding(
                                    get: { clipEvents[idx].time },
                                    set: { newVal in
                                        clipEvents[idx].time = newVal
                                        commitEvents()
                                    }), formatter: NumberFormatter.floatFormatter)
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 72)
                                .font(EditorTheme.font(size: 10))

                                Button(action: {
                                    clipEvents.remove(at: idx)
                                    commitEvents()
                                }) {
                                    Image(systemName: "trash")
                                }
                                .buttonStyle(.borderless)
                            }
                        }
                    }

                    HStack(spacing: 8) {
                        TextField("Event name", text: $newEventName)
                            .textFieldStyle(.roundedBorder)
                            .font(EditorTheme.font(size: 10))

                        TextField("Time", value: $newEventTime, formatter: NumberFormatter.floatFormatter)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 72)
                            .font(EditorTheme.font(size: 10))

                        Button("Add") {
                            addEvent()
                        }
                        .buttonStyle(.borderless)
                        .font(EditorTheme.font(size: 10, weight: .semibold))
                    }

                    if !eventLog.isEmpty {
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Recent Events")
                                .font(EditorTheme.font(size: 10, weight: .semibold))
                                .foregroundColor(EditorTheme.textMuted)
                            ForEach(eventLog) { evt in
                                Text(String(format: "%.2fs • %@", evt.time, evt.name))
                                    .font(EditorTheme.mono(size: 9))
                                    .foregroundColor(EditorTheme.textMuted)
                            }
                        }
                    }
                }
            }
        }
        .onAppear {
            refreshAnimation()
        }
        .onReceive(timer) { _ in
            refreshAnimation()
        }
    }
    
    private func refreshAnimation() {
        let bridge = CrescentEngineBridge.shared()
        guard let info = bridge.getSkinnedMeshInfo(uuid: entityUUID) as? [String: Any], !info.isEmpty else {
            clipNames = []
            stateNames = []
            clipEvents = []
            eventLog = []
            return
        }
        if let clips = info["clips"] as? [String] {
            clipNames = clips
        } else if let clips = info["clips"] as? [Any] {
            clipNames = clips.compactMap { $0 as? String }
        }
        
        if let states = info["states"] as? [String] {
            stateNames = states
        } else if let states = info["states"] as? [Any] {
            stateNames = states.compactMap { $0 as? String }
        } else {
            stateNames = []
        }

        rootMotionEnabled = (info["rootMotionEnabled"] as? NSNumber)?.boolValue ?? rootMotionEnabled
        rootMotionPosition = (info["rootMotionPosition"] as? NSNumber)?.boolValue ?? rootMotionPosition
        rootMotionRotation = (info["rootMotionRotation"] as? NSNumber)?.boolValue ?? rootMotionRotation

        parameters = []
        if let params = info["parameters"] as? [[String: Any]] {
            for param in params {
                let name = (param["name"] as? String) ?? ""
                let type = (param["type"] as? String) ?? "Float"
                let floatValue = (param["float"] as? NSNumber)?.floatValue ?? 0.0
                let intValue = (param["int"] as? NSNumber)?.intValue ?? 0
                let boolValue = (param["bool"] as? NSNumber)?.boolValue ?? false
                if !name.isEmpty {
                    parameters.append(AnimatorParam(
                        id: name,
                        name: name,
                        type: type,
                        floatValue: floatValue,
                        intValue: intValue,
                        boolValue: boolValue
                    ))
                }
            }
        }
        
        playing = (info["playing"] as? NSNumber)?.boolValue ?? playing
        looping = (info["looping"] as? NSNumber)?.boolValue ?? looping
        speed = (info["speed"] as? NSNumber)?.floatValue ?? speed
        time = (info["time"] as? NSNumber)?.floatValue ?? time
        duration = (info["duration"] as? NSNumber)?.floatValue ?? duration
        blendDuration = (info["blendDuration"] as? NSNumber)?.floatValue ?? blendDuration
        
        let incomingIndex = (info["clipIndex"] as? NSNumber)?.intValue ?? clipIndex
        if !clipNames.isEmpty {
            clipIndex = max(0, min(incomingIndex, clipNames.count - 1))
        }
        
        let incomingStateIndex = (info["stateIndex"] as? NSNumber)?.intValue ?? stateIndex
        if !stateNames.isEmpty {
            stateIndex = max(0, min(incomingStateIndex, stateNames.count - 1))
        }

        refreshEvents()
    }
    
    private func push(_ payload: [String: Any]) {
        _ = CrescentEngineBridge.shared().setSkinnedMeshInfo(uuid: entityUUID, info: payload)
    }

    private func setAnimatorParameter(_ name: String, type: String, value: Any) {
        _ = CrescentEngineBridge.shared().setAnimatorParameter(uuid: entityUUID, name: name, type: type, value: value)
    }

    private func updateRootMotion() {
        _ = CrescentEngineBridge.shared().setAnimatorRootMotion(
            uuid: entityUUID,
            enabled: rootMotionEnabled,
            applyPosition: rootMotionPosition,
            applyRotation: rootMotionRotation
        )
    }

    private func refreshEvents() {
        let bridge = CrescentEngineBridge.shared()
        if let events = bridge.getAnimationEvents(uuid: entityUUID, clipIndex: clipIndex) as? [[String: Any]] {
            var items: [AnimationEventItem] = []
            for entry in events {
                let name = (entry["name"] as? String) ?? ""
                let time = (entry["time"] as? NSNumber)?.floatValue ?? 0.0
                if !name.isEmpty {
                    items.append(AnimationEventItem(name: name, time: time))
                }
            }
            clipEvents = items.sorted { $0.time < $1.time }
        }

        if let fired = bridge.pollAnimatorEvents(uuid: entityUUID) as? [[String: Any]], !fired.isEmpty {
            for entry in fired {
                let name = (entry["name"] as? String) ?? ""
                let time = (entry["time"] as? NSNumber)?.floatValue ?? 0.0
                if !name.isEmpty {
                    eventLog.append(AnimationEventItem(name: name, time: time))
                }
            }
            if eventLog.count > 6 {
                eventLog.removeFirst(eventLog.count - 6)
            }
        }
    }

    private func commitEvents() {
        let payload = clipEvents.map { ["name": $0.name, "time": $0.time] }
        _ = CrescentEngineBridge.shared().setAnimationEvents(uuid: entityUUID, clipIndex: clipIndex, events: payload)
    }

    private func addEvent() {
        let trimmed = newEventName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        let event = AnimationEventItem(name: trimmed, time: max(0.0, newEventTime))
        clipEvents.append(event)
        clipEvents.sort { $0.time < $1.time }
        commitEvents()
        newEventName = ""
    }
}

struct PhysicsInspector: View {
    let entityUUID: String

    @State private var hasRigidbody: Bool = false
    @State private var bodyType: String = "Dynamic"
    @State private var mass: Float = 1.0
    @State private var linearDamping: Float = 0.05
    @State private var angularDamping: Float = 0.05
    @State private var useGravity: Bool = true
    @State private var continuous: Bool = false
    @State private var allowSleep: Bool = true

    @State private var hasCollider: Bool = false
    @State private var shapeType: String = "Box"
    @State private var size: [Float] = [1, 1, 1]
    @State private var radius: Float = 0.5
    @State private var height: Float = 1.0
    @State private var center: [Float] = [0, 0, 0]
    @State private var isTrigger: Bool = false
    @State private var friction: Float = 0.5
    @State private var restitution: Float = 0.0
    @State private var frictionCombine: String = "Average"
    @State private var restitutionCombine: String = "Average"
    @State private var collisionLayer: Int = 0
    @State private var collisionMask: Int = Int(UInt32.max)
    @State private var debugDraw: Bool = false

    @State private var hasHealth: Bool = false
    @State private var healthMax: Float = 100.0
    @State private var healthCurrent: Float = 100.0
    @State private var healthDestroyOnDeath: Bool = true

    @State private var hasCharacterController: Bool = false
    @State private var ccRadius: Float = 0.5
    @State private var ccHeight: Float = 2.0
    @State private var ccSkinWidth: Float = 0.02
    @State private var ccMoveSpeed: Float = 5.0
    @State private var ccAcceleration: Float = 30.0
    @State private var ccAirAcceleration: Float = 10.0
    @State private var ccJumpSpeed: Float = 5.5
    @State private var ccGravity: Float = 20.0
    @State private var ccMaxFallSpeed: Float = 40.0
    @State private var ccGroundSnapSpeed: Float = 2.0
    @State private var ccStepOffset: Float = 0.4
    @State private var ccSlopeLimit: Float = 50.0
    @State private var ccSlopeSlideSpeed: Float = 8.0
    @State private var ccGroundCheckDistance: Float = 0.08
    @State private var ccUseInput: Bool = true
    @State private var ccUseGravity: Bool = true
    @State private var ccEnableStep: Bool = true
    @State private var ccEnableSlopeLimit: Bool = true
    @State private var ccSnapToGround: Bool = true
    @State private var ccCollisionMask: Int = Int(UInt32.max)

    @State private var hasFpsController: Bool = false
    @State private var fpsMouseSensitivity: Float = 0.002
    @State private var fpsInvertY: Bool = false
    @State private var fpsRequireLookButton: Bool = true
    @State private var fpsLookButton: Int = 1
    @State private var fpsMinPitch: Float = -89.0
    @State private var fpsMaxPitch: Float = 89.0
    @State private var fpsWalkSpeed: Float = 5.0
    @State private var fpsSprintMultiplier: Float = 1.6
    @State private var fpsEnableSprint: Bool = true
    @State private var fpsEnableCrouch: Bool = true
    @State private var fpsCrouchHeight: Float = 1.2
    @State private var fpsCrouchEyeHeight: Float = 1.0
    @State private var fpsCrouchSpeed: Float = 6.0
    @State private var fpsEyeHeight: Float = 1.6
    @State private var fpsUseEyeHeight: Bool = true
    @State private var fpsDriveCharacter: Bool = true
    @State private var fpsFireCooldown: Float = 0.12
    @State private var fpsFireDamage: Float = 25.0
    @State private var fpsFireRange: Float = 60.0
    @State private var fpsFireMask: Int = Int(UInt32.max)
    @State private var fpsFireHitTriggers: Bool = false
    @State private var fpsMuzzleTexturePath: String = ""
    @State private var showMuzzleFileImporter: Bool = false

    private let timer = Timer.publish(every: 0.8, on: .main, in: .common).autoconnect()
    private let bodyTypes = ["Static", "Dynamic", "Kinematic"]
    private let shapeTypes = ["Box", "Sphere", "Capsule", "Mesh"]
    private let combineModes = ["Average", "Min", "Multiply", "Max"]
    private let textureExtensions: Set<String> = [
        "png", "jpg", "jpeg", "tga", "bmp", "gif", "tif", "tiff", "ktx", "ktx2", "dds", "cube"
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Toggle("Debug Draw", isOn: Binding(
                get: { debugDraw },
                set: { newVal in
                    debugDraw = newVal
                    CrescentEngineBridge.shared().setPhysicsDebugDraw(enabled: newVal)
                }))
            .font(EditorTheme.font(size: 11, weight: .medium))

            rigidbodySection
            Divider()
                .overlay(EditorTheme.panelStroke)
            colliderSection
            Divider()
                .overlay(EditorTheme.panelStroke)
            healthSection
            Divider()
                .overlay(EditorTheme.panelStroke)
            characterControllerSection
            Divider()
                .overlay(EditorTheme.panelStroke)
            firstPersonControllerSection
        }
        .onAppear { refresh() }
        .fileImporter(isPresented: $showMuzzleFileImporter, allowedContentTypes: [.image]) { result in
            switch result {
            case .success(let url):
                applyMuzzleTextureURL(url)
            case .failure(let error):
                print("Muzzle texture import failed: \(error.localizedDescription)")
            }
        }
        .onReceive(timer) { _ in refresh() }
    }

    private var rigidbodySection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Rigidbody")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                if hasRigidbody {
                    Button(action: removeRigidbody) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            if !hasRigidbody {
                Button("Add Rigidbody") {
                    let _ = CrescentEngineBridge.shared().addRigidbody(uuid: entityUUID)
                    refresh()
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            } else {
                Picker("Type", selection: Binding(
                    get: { bodyType },
                    set: { newVal in
                        bodyType = newVal
                        pushRigidbody()
                    })) {
                    ForEach(bodyTypes, id: \.self) { type in
                        Text(type).tag(type)
                    }
                }
                .pickerStyle(.menu)
                .font(EditorTheme.font(size: 11))

                SliderRow(title: "Mass", value: $mass, range: 0.1...200, step: 0.1, tint: .orange) { _ in
                    pushRigidbody()
                }

                SliderRow(title: "Linear Damping", value: $linearDamping, range: 0...2, step: 0.01) { _ in
                    pushRigidbody()
                }

                SliderRow(title: "Angular Damping", value: $angularDamping, range: 0...2, step: 0.01) { _ in
                    pushRigidbody()
                }

                Toggle("Use Gravity", isOn: Binding(
                    get: { useGravity },
                    set: { newVal in
                        useGravity = newVal
                        pushRigidbody()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Continuous Collision", isOn: Binding(
                    get: { continuous },
                    set: { newVal in
                        continuous = newVal
                        pushRigidbody()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Allow Sleep", isOn: Binding(
                    get: { allowSleep },
                    set: { newVal in
                        allowSleep = newVal
                        pushRigidbody()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))
            }
        }
    }

    private var colliderSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Collider")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                if hasCollider {
                    Button(action: removeCollider) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            if !hasCollider {
                Button("Add Collider") {
                    let _ = CrescentEngineBridge.shared().addCollider(uuid: entityUUID)
                    refresh()
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            } else {
                Picker("Shape", selection: Binding(
                    get: { shapeType },
                    set: { newVal in
                        shapeType = newVal
                        pushCollider()
                    })) {
                    ForEach(shapeTypes, id: \.self) { shape in
                        Text(shape).tag(shape)
                    }
                }
                .pickerStyle(.menu)
                .font(EditorTheme.font(size: 11))

                if shapeType == "Box" {
                    Vector3InputRow(title: "Size", values: $size) {
                        pushCollider()
                    }
                } else if shapeType == "Sphere" {
                    SliderRow(title: "Radius", value: $radius, range: 0.01...10, step: 0.01) { _ in
                        pushCollider()
                    }
                } else if shapeType == "Capsule" {
                    SliderRow(title: "Radius", value: $radius, range: 0.01...10, step: 0.01) { _ in
                        pushCollider()
                    }
                    SliderRow(title: "Height", value: $height, range: 0.1...20, step: 0.05) { _ in
                        pushCollider()
                    }
                } else {
                    Text("Mesh collider uses the MeshRenderer on this entity or a child named with 'collider'.")
                        .font(EditorTheme.font(size: 10, weight: .medium))
                        .foregroundColor(EditorTheme.textMuted)
                }

                Vector3InputRow(title: "Center", values: $center) {
                    pushCollider()
                }

                Toggle("Is Trigger", isOn: Binding(
                    get: { isTrigger },
                    set: { newVal in
                        isTrigger = newVal
                        pushCollider()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Picker("Friction Combine", selection: Binding(
                    get: { frictionCombine },
                    set: { newVal in
                        frictionCombine = newVal
                        pushCollider()
                    })) {
                    ForEach(combineModes, id: \.self) { mode in
                        Text(mode).tag(mode)
                    }
                }
                .pickerStyle(.menu)
                .font(EditorTheme.font(size: 11))

                Picker("Restitution Combine", selection: Binding(
                    get: { restitutionCombine },
                    set: { newVal in
                        restitutionCombine = newVal
                        pushCollider()
                    })) {
                    ForEach(combineModes, id: \.self) { mode in
                        Text(mode).tag(mode)
                    }
                }
                .pickerStyle(.menu)
                .font(EditorTheme.font(size: 11))

                HStack {
                    Text("Collision Layer")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                    Spacer()
                    TextField("0",
                              value: Binding(
                                get: { collisionLayer },
                                set: { newVal in
                                    collisionLayer = max(0, min(newVal, 31))
                                    pushCollider()
                                }),
                              formatter: NumberFormatter.intFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 72)
                }

                HStack {
                    Text("Collision Mask")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                    Spacer()
                    TextField("4294967295",
                              value: Binding(
                                get: { collisionMask },
                                set: { newVal in
                                    let clamped = max(0, min(newVal, Int(UInt32.max)))
                                    collisionMask = clamped
                                    pushCollider()
                                }),
                              formatter: NumberFormatter.intFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 120)
                }

                SliderRow(title: "Friction", value: $friction, range: 0...2, step: 0.01) { _ in
                    pushCollider()
                }

                SliderRow(title: "Restitution", value: $restitution, range: 0...1, step: 0.01) { _ in
                    pushCollider()
                }
            }
        }
    }

    private var healthSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Health")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                if hasHealth {
                    Button(action: removeHealth) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            if !hasHealth {
                Button("Add Health") {
                    let _ = CrescentEngineBridge.shared().addHealth(uuid: entityUUID)
                    refresh()
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            } else {
                SliderRow(title: "Max Health", value: $healthMax, range: 1...500, step: 1) { _ in
                    healthMax = max(1.0, healthMax)
                    if healthCurrent > healthMax {
                        healthCurrent = healthMax
                    }
                    pushHealth()
                }
                SliderRow(title: "Current Health", value: $healthCurrent, range: 0...max(1.0, healthMax), step: 1) { _ in
                    healthCurrent = min(healthCurrent, healthMax)
                    pushHealth()
                }

                Toggle("Destroy On Death", isOn: Binding(
                    get: { healthDestroyOnDeath },
                    set: { newVal in
                        healthDestroyOnDeath = newVal
                        pushHealth()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))
            }
        }
    }

    private var characterControllerSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Character Controller")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                if hasCharacterController {
                    Button(action: removeCharacterController) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            if !hasCharacterController {
                Button("Add Character Controller") {
                    let _ = CrescentEngineBridge.shared().addCharacterController(uuid: entityUUID)
                    refresh()
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            } else {
                SliderRow(title: "Radius", value: $ccRadius, range: 0.1...5, step: 0.01) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Height", value: $ccHeight, range: 0.5...10, step: 0.01) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Skin Width", value: $ccSkinWidth, range: 0...0.2, step: 0.001) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Move Speed", value: $ccMoveSpeed, range: 0...20, step: 0.1) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Acceleration", value: $ccAcceleration, range: 0...100, step: 0.5) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Air Acceleration", value: $ccAirAcceleration, range: 0...100, step: 0.5) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Jump Speed", value: $ccJumpSpeed, range: 0...20, step: 0.1) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Gravity", value: $ccGravity, range: 0...50, step: 0.1) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Max Fall Speed", value: $ccMaxFallSpeed, range: 0...100, step: 0.5) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Ground Snap Speed", value: $ccGroundSnapSpeed, range: 0...10, step: 0.1) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Step Offset", value: $ccStepOffset, range: 0...2, step: 0.01) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Slope Limit", value: $ccSlopeLimit, range: 0...89, step: 0.5) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Slope Slide Speed", value: $ccSlopeSlideSpeed, range: 0...20, step: 0.1) { _ in
                    pushCharacterController()
                }
                SliderRow(title: "Ground Check Dist", value: $ccGroundCheckDistance, range: 0...0.5, step: 0.005) { _ in
                    pushCharacterController()
                }

                Toggle("Use Input", isOn: Binding(
                    get: { ccUseInput },
                    set: { newVal in
                        ccUseInput = newVal
                        pushCharacterController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Use Gravity", isOn: Binding(
                    get: { ccUseGravity },
                    set: { newVal in
                        ccUseGravity = newVal
                        pushCharacterController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Enable Step", isOn: Binding(
                    get: { ccEnableStep },
                    set: { newVal in
                        ccEnableStep = newVal
                        pushCharacterController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Enable Slope Limit", isOn: Binding(
                    get: { ccEnableSlopeLimit },
                    set: { newVal in
                        ccEnableSlopeLimit = newVal
                        pushCharacterController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Snap To Ground", isOn: Binding(
                    get: { ccSnapToGround },
                    set: { newVal in
                        ccSnapToGround = newVal
                        pushCharacterController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                HStack {
                    Text("Collision Mask")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                    Spacer()
                    TextField("4294967295",
                              value: Binding(
                                get: { ccCollisionMask },
                                set: { newVal in
                                    let clamped = max(0, min(newVal, Int(UInt32.max)))
                                    ccCollisionMask = clamped
                                    pushCharacterController()
                                }),
                              formatter: NumberFormatter.intFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 120)
                }
            }
        }
    }

    private var firstPersonControllerSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("First Person Controller")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                if hasFpsController {
                    Button(action: removeFirstPersonController) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            if !hasFpsController {
                Button("Add First Person Controller") {
                    let _ = CrescentEngineBridge.shared().addFirstPersonController(uuid: entityUUID)
                    refresh()
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            } else {
                SliderRow(title: "Mouse Sensitivity", value: $fpsMouseSensitivity, range: 0.0005...0.01, step: 0.0001) { _ in
                    pushFirstPersonController()
                }

                Toggle("Invert Y", isOn: Binding(
                    get: { fpsInvertY },
                    set: { newVal in
                        fpsInvertY = newVal
                        pushFirstPersonController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Require Look Button", isOn: Binding(
                    get: { fpsRequireLookButton },
                    set: { newVal in
                        fpsRequireLookButton = newVal
                        pushFirstPersonController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                HStack {
                    Text("Look Button")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                    Spacer()
                    Picker("", selection: Binding(
                        get: { fpsLookButton },
                        set: { newVal in
                            fpsLookButton = newVal
                            pushFirstPersonController()
                        })) {
                        Text("Left").tag(0)
                        Text("Right").tag(1)
                        Text("Middle").tag(2)
                    }
                    .pickerStyle(.menu)
                    .frame(width: 120)
                }

                SliderRow(title: "Min Pitch", value: $fpsMinPitch, range: -89...0, step: 0.5) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Max Pitch", value: $fpsMaxPitch, range: 0...89, step: 0.5) { _ in
                    pushFirstPersonController()
                }

                SliderRow(title: "Walk Speed", value: $fpsWalkSpeed, range: 0...20, step: 0.1) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Sprint Multiplier", value: $fpsSprintMultiplier, range: 1...3, step: 0.05) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Fire Cooldown", value: $fpsFireCooldown, range: 0...0.5, step: 0.01) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Fire Damage", value: $fpsFireDamage, range: 0...200, step: 1) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Fire Range", value: $fpsFireRange, range: 1...200, step: 1) { _ in
                    pushFirstPersonController()
                }
                HStack {
                    Text("Hit Mask")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                    Spacer()
                    TextField("4294967295",
                              value: Binding(
                                get: { fpsFireMask },
                                set: { newVal in
                                    let clamped = max(0, min(newVal, Int(UInt32.max)))
                                    fpsFireMask = clamped
                                    pushFirstPersonController()
                                }),
                              formatter: NumberFormatter.intFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 120)
                }
                Toggle("Hit Triggers", isOn: Binding(
                    get: { fpsFireHitTriggers },
                    set: { newVal in
                        fpsFireHitTriggers = newVal
                        pushFirstPersonController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))
                TextureSlotRow(
                    title: "Muzzle Texture",
                    path: fpsMuzzleTexturePath
                ) {
                    showMuzzleFileImporter = true
                } onDrop: { url in
                    applyMuzzleTextureURL(url)
                } onClear: {
                    fpsMuzzleTexturePath = ""
                    pushFirstPersonController()
                }

                Toggle("Enable Sprint", isOn: Binding(
                    get: { fpsEnableSprint },
                    set: { newVal in
                        fpsEnableSprint = newVal
                        pushFirstPersonController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Enable Crouch", isOn: Binding(
                    get: { fpsEnableCrouch },
                    set: { newVal in
                        fpsEnableCrouch = newVal
                        pushFirstPersonController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                SliderRow(title: "Crouch Height", value: $fpsCrouchHeight, range: 0.5...2.0, step: 0.01) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Crouch Eye Height", value: $fpsCrouchEyeHeight, range: 0.2...2.0, step: 0.01) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Crouch Speed", value: $fpsCrouchSpeed, range: 0...12, step: 0.1) { _ in
                    pushFirstPersonController()
                }
                SliderRow(title: "Eye Height", value: $fpsEyeHeight, range: 0.2...2.5, step: 0.01) { _ in
                    pushFirstPersonController()
                }

                Toggle("Use Eye Height", isOn: Binding(
                    get: { fpsUseEyeHeight },
                    set: { newVal in
                        fpsUseEyeHeight = newVal
                        pushFirstPersonController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Drive Character Controller", isOn: Binding(
                    get: { fpsDriveCharacter },
                    set: { newVal in
                        fpsDriveCharacter = newVal
                        pushFirstPersonController()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))
            }
        }
    }

    private func refresh() {
        let bridge = CrescentEngineBridge.shared()
        if let info = bridge.getRigidbodyInfo(uuid: entityUUID) as? [String: Any],
           !info.isEmpty {
            hasRigidbody = true
            bodyType = info["type"] as? String ?? bodyType
            mass = (info["mass"] as? NSNumber)?.floatValue ?? mass
            linearDamping = (info["linearDamping"] as? NSNumber)?.floatValue ?? linearDamping
            angularDamping = (info["angularDamping"] as? NSNumber)?.floatValue ?? angularDamping
            useGravity = (info["useGravity"] as? NSNumber)?.boolValue ?? useGravity
            continuous = (info["continuous"] as? NSNumber)?.boolValue ?? continuous
            allowSleep = (info["allowSleep"] as? NSNumber)?.boolValue ?? allowSleep
        } else {
            hasRigidbody = false
        }

        if let info = bridge.getColliderInfo(uuid: entityUUID) as? [String: Any],
           !info.isEmpty {
            hasCollider = true
            shapeType = info["shape"] as? String ?? shapeType
            if let sizeValues = info["size"] as? [NSNumber], sizeValues.count >= 3 {
                size = sizeValues.prefix(3).map { $0.floatValue }
            }
            radius = (info["radius"] as? NSNumber)?.floatValue ?? radius
            height = (info["height"] as? NSNumber)?.floatValue ?? height
            if let centerValues = info["center"] as? [NSNumber], centerValues.count >= 3 {
                center = centerValues.prefix(3).map { $0.floatValue }
            }
            isTrigger = (info["trigger"] as? NSNumber)?.boolValue ?? isTrigger
            friction = (info["friction"] as? NSNumber)?.floatValue ?? friction
            restitution = (info["restitution"] as? NSNumber)?.floatValue ?? restitution
            frictionCombine = info["frictionCombine"] as? String ?? frictionCombine
            restitutionCombine = info["restitutionCombine"] as? String ?? restitutionCombine
            collisionLayer = (info["layer"] as? NSNumber)?.intValue ?? collisionLayer
            collisionMask = (info["mask"] as? NSNumber)?.intValue ?? collisionMask
        } else {
            hasCollider = false
        }

        if let info = bridge.getHealthInfo(uuid: entityUUID) as? [String: Any],
           !info.isEmpty {
            hasHealth = true
            healthMax = (info["max"] as? NSNumber)?.floatValue ?? healthMax
            healthCurrent = (info["current"] as? NSNumber)?.floatValue ?? healthCurrent
            healthDestroyOnDeath = (info["destroyOnDeath"] as? NSNumber)?.boolValue ?? healthDestroyOnDeath
        } else {
            hasHealth = false
        }

        if let info = bridge.getCharacterControllerInfo(uuid: entityUUID) as? [String: Any],
           !info.isEmpty {
            hasCharacterController = true
            ccRadius = (info["radius"] as? NSNumber)?.floatValue ?? ccRadius
            ccHeight = (info["height"] as? NSNumber)?.floatValue ?? ccHeight
            ccSkinWidth = (info["skinWidth"] as? NSNumber)?.floatValue ?? ccSkinWidth
            ccMoveSpeed = (info["moveSpeed"] as? NSNumber)?.floatValue ?? ccMoveSpeed
            ccAcceleration = (info["acceleration"] as? NSNumber)?.floatValue ?? ccAcceleration
            ccAirAcceleration = (info["airAcceleration"] as? NSNumber)?.floatValue ?? ccAirAcceleration
            ccJumpSpeed = (info["jumpSpeed"] as? NSNumber)?.floatValue ?? ccJumpSpeed
            ccGravity = (info["gravity"] as? NSNumber)?.floatValue ?? ccGravity
            ccMaxFallSpeed = (info["maxFallSpeed"] as? NSNumber)?.floatValue ?? ccMaxFallSpeed
            ccGroundSnapSpeed = (info["groundSnapSpeed"] as? NSNumber)?.floatValue ?? ccGroundSnapSpeed
            ccStepOffset = (info["stepOffset"] as? NSNumber)?.floatValue ?? ccStepOffset
            ccSlopeLimit = (info["slopeLimit"] as? NSNumber)?.floatValue ?? ccSlopeLimit
            ccSlopeSlideSpeed = (info["slopeSlideSpeed"] as? NSNumber)?.floatValue ?? ccSlopeSlideSpeed
            ccGroundCheckDistance = (info["groundCheckDistance"] as? NSNumber)?.floatValue ?? ccGroundCheckDistance
            ccUseInput = (info["useInput"] as? NSNumber)?.boolValue ?? ccUseInput
            ccUseGravity = (info["useGravity"] as? NSNumber)?.boolValue ?? ccUseGravity
            ccEnableStep = (info["enableStep"] as? NSNumber)?.boolValue ?? ccEnableStep
            ccEnableSlopeLimit = (info["enableSlopeLimit"] as? NSNumber)?.boolValue ?? ccEnableSlopeLimit
            ccSnapToGround = (info["snapToGround"] as? NSNumber)?.boolValue ?? ccSnapToGround
            ccCollisionMask = (info["collisionMask"] as? NSNumber)?.intValue ?? ccCollisionMask
        } else {
            hasCharacterController = false
        }

        if let info = bridge.getFirstPersonControllerInfo(uuid: entityUUID) as? [String: Any],
           !info.isEmpty {
            hasFpsController = true
            fpsMouseSensitivity = (info["mouseSensitivity"] as? NSNumber)?.floatValue ?? fpsMouseSensitivity
            fpsInvertY = (info["invertY"] as? NSNumber)?.boolValue ?? fpsInvertY
            fpsRequireLookButton = (info["requireLookButton"] as? NSNumber)?.boolValue ?? fpsRequireLookButton
            fpsLookButton = (info["lookButton"] as? NSNumber)?.intValue ?? fpsLookButton
            fpsMinPitch = (info["minPitch"] as? NSNumber)?.floatValue ?? fpsMinPitch
            fpsMaxPitch = (info["maxPitch"] as? NSNumber)?.floatValue ?? fpsMaxPitch
            fpsWalkSpeed = (info["walkSpeed"] as? NSNumber)?.floatValue ?? fpsWalkSpeed
            fpsSprintMultiplier = (info["sprintMultiplier"] as? NSNumber)?.floatValue ?? fpsSprintMultiplier
            fpsEnableSprint = (info["enableSprint"] as? NSNumber)?.boolValue ?? fpsEnableSprint
            fpsEnableCrouch = (info["enableCrouch"] as? NSNumber)?.boolValue ?? fpsEnableCrouch
            fpsCrouchHeight = (info["crouchHeight"] as? NSNumber)?.floatValue ?? fpsCrouchHeight
            fpsCrouchEyeHeight = (info["crouchEyeHeight"] as? NSNumber)?.floatValue ?? fpsCrouchEyeHeight
            fpsCrouchSpeed = (info["crouchSpeed"] as? NSNumber)?.floatValue ?? fpsCrouchSpeed
            fpsEyeHeight = (info["eyeHeight"] as? NSNumber)?.floatValue ?? fpsEyeHeight
            fpsUseEyeHeight = (info["useEyeHeight"] as? NSNumber)?.boolValue ?? fpsUseEyeHeight
            fpsDriveCharacter = (info["driveCharacterController"] as? NSNumber)?.boolValue ?? fpsDriveCharacter
            fpsFireCooldown = (info["fireCooldown"] as? NSNumber)?.floatValue ?? fpsFireCooldown
            fpsFireDamage = (info["fireDamage"] as? NSNumber)?.floatValue ?? fpsFireDamage
            fpsFireRange = (info["fireRange"] as? NSNumber)?.floatValue ?? fpsFireRange
            fpsFireMask = (info["fireMask"] as? NSNumber)?.intValue ?? fpsFireMask
            fpsFireHitTriggers = (info["fireHitTriggers"] as? NSNumber)?.boolValue ?? fpsFireHitTriggers
            fpsMuzzleTexturePath = info["muzzleTexture"] as? String ?? fpsMuzzleTexturePath
        } else {
            hasFpsController = false
        }

        debugDraw = bridge.getPhysicsDebugDraw()
    }

    private func pushRigidbody() {
        let info: [String: Any] = [
            "type": bodyType,
            "mass": mass,
            "linearDamping": linearDamping,
            "angularDamping": angularDamping,
            "useGravity": useGravity,
            "continuous": continuous,
            "allowSleep": allowSleep
        ]
        _ = CrescentEngineBridge.shared().setRigidbodyInfo(uuid: entityUUID, info: info)
    }

    private func pushCollider() {
        let info: [String: Any] = [
            "shape": shapeType,
            "size": size,
            "radius": radius,
            "height": height,
            "center": center,
            "trigger": isTrigger,
            "friction": friction,
            "restitution": restitution,
            "frictionCombine": frictionCombine,
            "restitutionCombine": restitutionCombine,
            "layer": collisionLayer,
            "mask": collisionMask
        ]
        _ = CrescentEngineBridge.shared().setColliderInfo(uuid: entityUUID, info: info)
    }

    private func pushHealth() {
        let info: [String: Any] = [
            "max": healthMax,
            "current": healthCurrent,
            "destroyOnDeath": healthDestroyOnDeath
        ]
        _ = CrescentEngineBridge.shared().setHealthInfo(uuid: entityUUID, info: info)
    }

    private func removeRigidbody() {
        CrescentEngineBridge.shared().removeRigidbody(uuid: entityUUID)
        hasRigidbody = false
    }

    private func removeCollider() {
        CrescentEngineBridge.shared().removeCollider(uuid: entityUUID)
        hasCollider = false
    }

    private func removeHealth() {
        CrescentEngineBridge.shared().removeHealth(uuid: entityUUID)
        hasHealth = false
    }

    private func pushCharacterController() {
        let info: [String: Any] = [
            "radius": ccRadius,
            "height": ccHeight,
            "skinWidth": ccSkinWidth,
            "moveSpeed": ccMoveSpeed,
            "acceleration": ccAcceleration,
            "airAcceleration": ccAirAcceleration,
            "jumpSpeed": ccJumpSpeed,
            "gravity": ccGravity,
            "maxFallSpeed": ccMaxFallSpeed,
            "groundSnapSpeed": ccGroundSnapSpeed,
            "stepOffset": ccStepOffset,
            "slopeLimit": ccSlopeLimit,
            "slopeSlideSpeed": ccSlopeSlideSpeed,
            "groundCheckDistance": ccGroundCheckDistance,
            "useInput": ccUseInput,
            "useGravity": ccUseGravity,
            "enableStep": ccEnableStep,
            "enableSlopeLimit": ccEnableSlopeLimit,
            "snapToGround": ccSnapToGround,
            "collisionMask": ccCollisionMask
        ]
        _ = CrescentEngineBridge.shared().setCharacterControllerInfo(uuid: entityUUID, info: info)
    }

    private func removeCharacterController() {
        CrescentEngineBridge.shared().removeCharacterController(uuid: entityUUID)
        hasCharacterController = false
    }

    private func pushFirstPersonController() {
        let info: [String: Any] = [
            "mouseSensitivity": fpsMouseSensitivity,
            "invertY": fpsInvertY,
            "requireLookButton": fpsRequireLookButton,
            "lookButton": fpsLookButton,
            "minPitch": fpsMinPitch,
            "maxPitch": fpsMaxPitch,
            "walkSpeed": fpsWalkSpeed,
            "sprintMultiplier": fpsSprintMultiplier,
            "enableSprint": fpsEnableSprint,
            "enableCrouch": fpsEnableCrouch,
            "crouchHeight": fpsCrouchHeight,
            "crouchEyeHeight": fpsCrouchEyeHeight,
            "crouchSpeed": fpsCrouchSpeed,
            "eyeHeight": fpsEyeHeight,
            "useEyeHeight": fpsUseEyeHeight,
            "driveCharacterController": fpsDriveCharacter,
            "fireCooldown": fpsFireCooldown,
            "fireDamage": fpsFireDamage,
            "fireRange": fpsFireRange,
            "fireMask": fpsFireMask,
            "fireHitTriggers": fpsFireHitTriggers,
            "muzzleTexture": fpsMuzzleTexturePath
        ]
        _ = CrescentEngineBridge.shared().setFirstPersonControllerInfo(uuid: entityUUID, info: info)
    }

    private func removeFirstPersonController() {
        CrescentEngineBridge.shared().removeFirstPersonController(uuid: entityUUID)
        hasFpsController = false
    }

    private func applyMuzzleTextureURL(_ url: URL) {
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

        let ext = resolvedURL.pathExtension.lowercased()
        if !textureExtensions.contains(ext) {
            print("Unsupported muzzle texture type: \(resolvedURL.lastPathComponent)")
            return
        }

        var path = resolvedURL.path
        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "texture")
        if !imported.isEmpty {
            path = imported
        }

        fpsMuzzleTexturePath = path
        pushFirstPersonController()
    }
}

struct AudioInspector: View {
    let entityUUID: String

    @State private var hasAudioSource: Bool = false
    @State private var audioPath: String = ""
    @State private var volume: Float = 1.0
    @State private var pitch: Float = 1.0
    @State private var looping: Bool = false
    @State private var playOnStart: Bool = false
    @State private var spatial: Bool = true
    @State private var stream: Bool = false
    @State private var minDistance: Float = 1.0
    @State private var maxDistance: Float = 50.0
    @State private var rolloff: Float = 1.0
    @State private var showFileImporter: Bool = false
    @State private var isDropping: Bool = false

    private let audioExtensions = ["wav", "mp3", "ogg", "flac"]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Audio Source")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                Spacer()
                if hasAudioSource {
                    Button(action: removeAudioSource) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            if !hasAudioSource {
                Button("Add Audio Source") {
                    let _ = CrescentEngineBridge.shared().addAudioSource(uuid: entityUUID)
                    refresh()
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            } else {
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("Clip")
                            .font(EditorTheme.font(size: 11, weight: .medium))
                        Spacer()
                        Button("Load") {
                            showFileImporter = true
                        }
                        .buttonStyle(.borderless)
                        Button("Clear") {
                            audioPath = ""
                            pushAudioSource()
                        }
                        .buttonStyle(.borderless)
                        .foregroundColor(.red.opacity(0.9))
                    }
                    Text(audioPath.isEmpty ? "No clip assigned" : audioPath)
                        .font(EditorTheme.mono(size: 10))
                        .foregroundColor(audioPath.isEmpty ? EditorTheme.textMuted : EditorTheme.textPrimary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                .padding(8)
                .background(EditorTheme.surface)
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(isDropping ? EditorTheme.textAccent : EditorTheme.panelStroke, lineWidth: 1)
                )
                .cornerRadius(6)
                .onDrop(of: [UTType.fileURL], isTargeted: $isDropping) { providers in
                    for provider in providers where provider.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) {
                        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                            DispatchQueue.main.async {
                                if let data = item as? Data,
                                   let url = URL(dataRepresentation: data, relativeTo: nil) {
                                    applyAudioURL(url)
                                } else if let url = item as? URL {
                                    applyAudioURL(url)
                                }
                            }
                        }
                    }
                    return true
                }

                SliderRow(title: "Volume", value: $volume, range: 0...2, step: 0.01) { _ in
                    pushAudioSource()
                }
                SliderRow(title: "Pitch", value: $pitch, range: 0.5...2, step: 0.01) { _ in
                    pushAudioSource()
                }

                Toggle("Loop", isOn: Binding(
                    get: { looping },
                    set: { newVal in
                        looping = newVal
                        pushAudioSource()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Play On Start", isOn: Binding(
                    get: { playOnStart },
                    set: { newVal in
                        playOnStart = newVal
                        pushAudioSource()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("3D Spatial", isOn: Binding(
                    get: { spatial },
                    set: { newVal in
                        spatial = newVal
                        pushAudioSource()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                Toggle("Stream From Disk", isOn: Binding(
                    get: { stream },
                    set: { newVal in
                        stream = newVal
                        pushAudioSource()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                if spatial {
                    SliderRow(title: "Min Distance", value: $minDistance, range: 0.1...50, step: 0.1) { _ in
                        minDistance = min(minDistance, maxDistance)
                        pushAudioSource()
                    }
                    SliderRow(title: "Max Distance", value: $maxDistance, range: 0.1...200, step: 0.5) { _ in
                        maxDistance = max(minDistance, maxDistance)
                        pushAudioSource()
                    }
                    SliderRow(title: "Rolloff", value: $rolloff, range: 0...4, step: 0.05) { _ in
                        pushAudioSource()
                    }
                }
            }
        }
        .onAppear { refresh() }
        .onChange(of: entityUUID) { _ in refresh() }
        .fileImporter(isPresented: $showFileImporter, allowedContentTypes: [.audio]) { result in
            handleAudioImport(result)
        }
    }

    private func refresh() {
        let bridge = CrescentEngineBridge.shared()
        if let info = bridge.getAudioSourceInfo(uuid: entityUUID) as? [String: Any],
           !info.isEmpty {
            hasAudioSource = true
            audioPath = info["filePath"] as? String ?? audioPath
            volume = (info["volume"] as? NSNumber)?.floatValue ?? volume
            pitch = (info["pitch"] as? NSNumber)?.floatValue ?? pitch
            looping = (info["looping"] as? NSNumber)?.boolValue ?? looping
            playOnStart = (info["playOnStart"] as? NSNumber)?.boolValue ?? playOnStart
            spatial = (info["spatial"] as? NSNumber)?.boolValue ?? spatial
            stream = (info["stream"] as? NSNumber)?.boolValue ?? stream
            minDistance = (info["minDistance"] as? NSNumber)?.floatValue ?? minDistance
            maxDistance = (info["maxDistance"] as? NSNumber)?.floatValue ?? maxDistance
            rolloff = (info["rolloff"] as? NSNumber)?.floatValue ?? rolloff
        } else {
            hasAudioSource = false
            audioPath = ""
            volume = 1.0
            pitch = 1.0
            looping = false
            playOnStart = false
            spatial = true
            stream = false
            minDistance = 1.0
            maxDistance = 50.0
            rolloff = 1.0
        }
    }

    private func pushAudioSource() {
        let info: [String: Any] = [
            "filePath": audioPath,
            "volume": volume,
            "pitch": pitch,
            "looping": looping,
            "playOnStart": playOnStart,
            "spatial": spatial,
            "stream": stream,
            "minDistance": minDistance,
            "maxDistance": maxDistance,
            "rolloff": rolloff
        ]
        _ = CrescentEngineBridge.shared().setAudioSourceInfo(uuid: entityUUID, info: info)
    }

    private func removeAudioSource() {
        CrescentEngineBridge.shared().removeAudioSource(uuid: entityUUID)
        hasAudioSource = false
    }

    private func handleAudioImport(_ result: Result<URL, Error>) {
        switch result {
        case .success(let url):
            applyAudioURL(url)
        case .failure(let error):
            print("Audio import failed: \(error.localizedDescription)")
        }
    }

    private func applyAudioURL(_ url: URL) {
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

        let ext = resolvedURL.pathExtension.lowercased()
        if !audioExtensions.contains(ext) {
            print("Unsupported audio type: \(resolvedURL.lastPathComponent)")
            return
        }

        var path = resolvedURL.path
        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "audio")
        if !imported.isEmpty {
            path = imported
        }

        audioPath = path
        pushAudioSource()
    }
}

struct VectorField: View {
    enum TransformType {
        case position, rotation, scale
    }
    
    let label: String
    @Binding var values: [Float]
    let color: Color
    let selectedUUIDs: Set<String>  // All selected UUIDs
    let transformType: TransformType
    
    @State private var selectedAxisIndex: Int? = nil  // ✅ Which axis is selected for slider
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(EditorTheme.font(size: 11, weight: .medium))
                .foregroundColor(EditorTheme.textMuted)
            
            HStack(spacing: 6) {
                AxisField(axis: "X", value: values[safe: 0] ?? 0, color: .red, isSelected: selectedAxisIndex == 0) { newValue in
                    let oldValue = values[safe: 0] ?? 0
                    if values.count > 0 { values[0] = newValue }
                    updateTransform(changedIndex: 0, oldValue: oldValue)
                } onTap: {
                    print("[INSPECTOR] X axis tapped - current selection: \(selectedAxisIndex ?? -1)")
                    selectedAxisIndex = (selectedAxisIndex == 0) ? nil : 0  // Toggle
                    print("[INSPECTOR] New selection: \(selectedAxisIndex ?? -1)")
                }
                
                AxisField(axis: "Y", value: values[safe: 1] ?? 0, color: .green, isSelected: selectedAxisIndex == 1) { newValue in
                    let oldValue = values[safe: 1] ?? 0
                    if values.count > 1 { values[1] = newValue }
                    updateTransform(changedIndex: 1, oldValue: oldValue)
                } onTap: {
                    selectedAxisIndex = (selectedAxisIndex == 1) ? nil : 1  // Toggle
                }
                
                AxisField(axis: "Z", value: values[safe: 2] ?? 0, color: .blue, isSelected: selectedAxisIndex == 2) { newValue in
                    let oldValue = values[safe: 2] ?? 0
                    if values.count > 2 { values[2] = newValue }
                    updateTransform(changedIndex: 2, oldValue: oldValue)
                } onTap: {
                    selectedAxisIndex = (selectedAxisIndex == 2) ? nil : 2  // Toggle
                }
            }
            
            // ✅ Show slider ONLY for selected axis
            if let axisIndex = selectedAxisIndex, values.indices.contains(axisIndex) {
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text(axisLabel(for: axisIndex))
                            .font(EditorTheme.font(size: 10, weight: .semibold))
                            .foregroundColor(axisColor(for: axisIndex))
                        Spacer()
                        Text(String(format: "%.3f", values[axisIndex]))
                            .font(EditorTheme.mono(size: 10))
                            .foregroundColor(EditorTheme.textMuted)
                    }
                    
                    Slider(value: Binding(
                        get: { Double(values[axisIndex]) },
                        set: { newVal in
                            let oldValue = values[axisIndex]
                            values[axisIndex] = Float(newVal)
                            updateTransform(changedIndex: axisIndex, oldValue: oldValue)
                        }),
                           in: sliderRange().lowerBound...sliderRange().upperBound)
                        .accentColor(colorForTransform())
                }
                .padding(.top, 4)
                .onAppear {
                    print("[INSPECTOR] Slider appeared for axis \(axisIndex) (\(axisLabel(for: axisIndex)))")
                }
            } else {
                // Debug: why isn't slider showing?
                let _ = print("[INSPECTOR] Slider hidden - selectedAxisIndex: \(selectedAxisIndex ?? -1), values.count: \(values.count)")
            }
        }
    }
    
    private func axisLabel(for index: Int) -> String {
        switch index {
        case 0: return "X Axis"
        case 1: return "Y Axis"
        case 2: return "Z Axis"
        default: return ""
        }
    }
    
    private func axisColor(for index: Int) -> Color {
        switch index {
        case 0: return .red
        case 1: return .green
        case 2: return .blue
        default: return .gray
        }
    }
    
    private func sliderRange() -> ClosedRange<Double> {
        switch transformType {
        case .position:
            return -100.0...100.0
        case .rotation:
            return -180.0...180.0
        case .scale:
            return 0.01...10.0
        }
    }
    
    private func colorForTransform() -> Color {
        switch transformType {
        case .position: return .orange
        case .rotation: return .purple
        case .scale: return .teal
        }
    }
    
    func updateTransform(changedIndex: Int, oldValue: Float) {
        guard values.count == 3 else { return }
        guard values.indices.contains(changedIndex) else { return }
        guard !selectedUUIDs.isEmpty else { return }
        let bridge = CrescentEngineBridge.shared()
        let delta = values[changedIndex] - oldValue
        if abs(delta) < 0.00001 { return }
        
        // Apply to ALL selected entities
        for uuid in selectedUUIDs {
            switch transformType {
            case .position:
                if let pos = bridge.getPosition(uuid: uuid) as? [NSNumber], pos.count >= 3 {
                    var x = pos[0].floatValue
                    var y = pos[1].floatValue
                    var z = pos[2].floatValue
                    if changedIndex == 0 { x += delta }
                    if changedIndex == 1 { y += delta }
                    if changedIndex == 2 { z += delta }
                    bridge.setPosition(uuid: uuid, x: x, y: y, z: z)
                }
            case .rotation:
                if let rot = bridge.getRotation(uuid: uuid) as? [NSNumber], rot.count >= 3 {
                    var x = rot[0].floatValue
                    var y = rot[1].floatValue
                    var z = rot[2].floatValue
                    if changedIndex == 0 { x += delta }
                    if changedIndex == 1 { y += delta }
                    if changedIndex == 2 { z += delta }
                    bridge.setRotation(uuid: uuid, x: x, y: y, z: z)
                }
            case .scale:
                if let scl = bridge.getScale(uuid: uuid) as? [NSNumber], scl.count >= 3 {
                    var x = scl[0].floatValue
                    var y = scl[1].floatValue
                    var z = scl[2].floatValue
                    if changedIndex == 0 { x += delta }
                    if changedIndex == 1 { y += delta }
                    if changedIndex == 2 { z += delta }
                    bridge.setScale(uuid: uuid, x: x, y: y, z: z)
                }
            }
        }
    }
}

extension Array {
    subscript(safe index: Int) -> Element? {
        return indices.contains(index) ? self[index] : nil
    }
}

struct AxisField: View {
    let axis: String
    let value: Float
    let color: Color
    let isSelected: Bool
    let onValueChange: (Float) -> Void
    let onTap: () -> Void  
    
    @State private var textValue: String = "0"
    @State private var isEditing: Bool = false
    @FocusState private var isFocused: Bool
    
    var body: some View {
        HStack(spacing: 4) {
            Text(axis)
                .font(EditorTheme.font(size: 10, weight: .bold))
                .foregroundColor(.white)
                .frame(width: 16, height: 20)
                .background(color)
                .cornerRadius(3)
            
            ZStack {
                // Background that captures taps
                Rectangle()
                    .fill(isSelected ? color.opacity(0.2) : EditorTheme.panelBackground)
                    .onTapGesture(count: 2) {
                        // Double-tap to edit (only when selected)
                        print("[INSPECTOR] Double-tap on \(axis) field")
                        if isSelected {
                            isFocused = true
                        }
                    }
                    .onTapGesture(count: 1) {
                        // Single tap to select
                        print("[INSPECTOR] Single-tap on \(axis) field - isSelected: \(isSelected)")
                        onTap()
                    }
                
                TextField("", text: $textValue, onEditingChanged: { editing in
                    isEditing = editing
                    if !editing {
                        if let floatValue = Float(textValue) {
                            onValueChange(floatValue)
                        } else {
                            textValue = String(format: "%.3f", value)
                        }
                    }
                })
                .focused($isFocused)
                .textFieldStyle(.plain)
                .font(EditorTheme.mono(size: 11))
                .multilineTextAlignment(.trailing)
                .padding(.horizontal, 6)
                .foregroundColor(EditorTheme.textPrimary)
                .disabled(!isSelected)  // ✅ Can only edit when selected
            }
            .frame(height: 20)
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(isSelected ? color : EditorTheme.panelStroke, lineWidth: isSelected ? 2 : 1)
            )
            .cornerRadius(4)
        }
        .onChange(of: value) { newValue in
            if !isEditing {
                textValue = String(format: "%.3f", newValue)
            }
        }
        .onAppear {
            textValue = String(format: "%.3f", value)
        }
    }
}

// MARK: - Material Inspector

private enum TextureSlot: String, CaseIterable {
    case albedo, normal, metallic, roughness, ao, orm, emission, height
    
    var displayName: String {
        switch self {
        case .albedo: return "Albedo / Base Color"
        case .normal: return "Normal Map"
        case .metallic: return "Metallic Map"
        case .roughness: return "Roughness Map"
        case .ao: return "Ambient Occlusion"
        case .orm: return "ORM (AO/Roughness/Metallic)"
        case .emission: return "Emission Map"
        case .height: return "Height Map"
        }
    }
}

struct MaterialInspector: View {
    let entityUUID: String
    let selectedUUIDs: Set<String>
    
    @State private var albedo: [Float] = [1, 1, 1, 1]
    @State private var emission: [Float] = [0, 0, 0]
    @State private var metallic: Float = 0
    @State private var roughness: Float = 0.5
    @State private var ao: Float = 1.0
    @State private var emissionStrength: Float = 0.0
    @State private var normalScale: Float = 1.0
    @State private var heightScale: Float = 0.02
    @State private var heightInvert: Bool = false
    @State private var renderMode: Int = 0
    @State private var alphaCutoff: Float = 0.5
    @State private var twoSided: Bool = false
    @State private var alphaToCoverage: Bool = false
    @State private var windEnabled: Bool = false
    @State private var windStrength: Float = 0.0
    @State private var windSpeed: Float = 1.0
    @State private var windScale: Float = 0.1
    @State private var windGust: Float = 0.0
    @State private var windDirection: [Float] = [1.0, 0.0, 0.0]
    @State private var lodFadeEnabled: Bool = false
    @State private var lodFadeStart: Float = 0.0
    @State private var lodFadeEnd: Float = 0.0
    @State private var ditherEnabled: Bool = true
    @State private var billboardEnabled: Bool = false
    @State private var billboardStart: Float = 0.0
    @State private var billboardEnd: Float = 0.0
    @State private var impostorEnabled: Bool = false
    @State private var impostorRows: Float = 8.0
    @State private var impostorCols: Float = 8.0
    @State private var tiling: [Float] = [1, 1]
    @State private var offset: [Float] = [0, 0]
    @State private var texturePaths: [String: String] = [:]
    private let textureExtensions: Set<String> = [
        "png", "jpg", "jpeg", "tga", "bmp", "gif", "tif", "tiff", "ktx", "ktx2", "dds"
    ]
    
    @State private var activeTextureSlot: TextureSlot?
    @State private var showFileImporter: Bool = false
    @State private var applyToAllSelected: Bool = false
    @State private var applyToAllMaterials: Bool = false
    
    private let timer = Timer.publish(every: 0.75, on: .main, in: .common).autoconnect()
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if selectedUUIDs.count > 1 {
                VStack(alignment: .leading, spacing: 6) {
                    Toggle(isOn: $applyToAllSelected) {
                        Text("Apply To Selection (\(selectedUUIDs.count))")
                            .font(EditorTheme.font(size: 11, weight: .medium))
                    }
                    Text("Edits are previewed from the primary selection.")
                        .font(EditorTheme.font(size: 10, weight: .regular))
                        .foregroundColor(EditorTheme.textMuted)
                }
            }

            Toggle(isOn: $applyToAllMaterials) {
                Text("Apply To All Materials")
                    .font(EditorTheme.font(size: 11, weight: .medium))
            }

            VStack(alignment: .leading, spacing: 8) {
                ColorPicker("Albedo", selection: Binding(
                    get: { colorFrom(albedo) },
                    set: { updateAlbedoColor($0) }
                ))
                .font(EditorTheme.font(size: 11, weight: .medium))
                
                ColorPicker("Emission Tint", selection: Binding(
                    get: { colorFrom(emission + [1]) },
                    set: { updateEmissionColor($0) }
                ))
                .font(EditorTheme.font(size: 11, weight: .medium))
            }
            
            SliderRow(title: "Metallic", value: $metallic, range: 0...1) { newVal in
                sendScalar(property: "metallic", value: newVal)
            }
            
            SliderRow(title: "Roughness", value: $roughness, range: 0.02...1) { newVal in
                sendScalar(property: "roughness", value: newVal)
            }
            
            SliderRow(title: "Ambient Occlusion", value: $ao, range: 0...1) { newVal in
                sendScalar(property: "ao", value: newVal)
            }
            
            SliderRow(title: "Normal Strength", value: $normalScale, range: 0...4, step: 0.05, tint: .purple) { newVal in
                sendScalar(property: "normalScale", value: newVal)
            }
            
            Toggle(isOn: Binding(
                get: { heightInvert },
                set: { newVal in
                    heightInvert = newVal
                    sendScalar(property: "heightInvert", value: newVal ? 1.0 : 0.0)
                })) {
                Text("Invert Height Map")
                    .font(EditorTheme.font(size: 11, weight: .medium))
            }
            
            SliderRow(title: "Height Scale", value: $heightScale, range: 0...0.1, step: 0.001, tint: .mint) { newVal in
                sendScalar(property: "heightScale", value: newVal)
            }
            
            SliderRow(title: "Emission Strength", value: $emissionStrength, range: 0...20, step: 0.1, tint: .orange) { newVal in
                sendScalar(property: "emissionStrength", value: newVal)
            }
            
            SliderRow(title: "Opacity", value: Binding(
                get: { albedo[safe: 3] ?? 1 },
                set: { newVal in
                    if albedo.count > 3 { albedo[3] = newVal }
                    sendScalar(property: "alpha", value: newVal)
                }), range: 0...1, step: 0.01, tint: .teal, onChange: { _ in })

            VStack(alignment: .leading, spacing: 6) {
                Text("Render Mode")
                    .font(EditorTheme.font(size: 11, weight: .medium))
                    .foregroundColor(EditorTheme.textPrimary)
                Picker("", selection: Binding(
                    get: { renderMode },
                    set: { newVal in
                        renderMode = newVal
                        sendScalar(property: "renderMode", value: Float(newVal))
                    })) {
                    Text("Opaque").tag(0)
                    Text("Transparent").tag(1)
                    Text("Cutout").tag(2)
                }
                .pickerStyle(.segmented)
            }

            if renderMode == 2 {
                SliderRow(title: "Alpha Cutoff", value: Binding(
                    get: { alphaCutoff },
                    set: { newVal in
                        alphaCutoff = newVal
                        sendScalar(property: "alphaCutoff", value: newVal)
                    }), range: 0...1, step: 0.01, tint: .pink, onChange: { _ in })

                Toggle(isOn: Binding(
                    get: { alphaToCoverage },
                    set: { newVal in
                        alphaToCoverage = newVal
                        sendScalar(property: "alphaToCoverage", value: newVal ? 1.0 : 0.0)
                    })) {
                    Text("Alpha To Coverage (MSAA)")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                }
            }

            Toggle(isOn: Binding(
                get: { twoSided },
                set: { newVal in
                    twoSided = newVal
                    sendScalar(property: "twoSided", value: newVal ? 1.0 : 0.0)
                })) {
                Text("Two-Sided")
                    .font(EditorTheme.font(size: 11, weight: .medium))
            }

            Divider()
                .overlay(EditorTheme.panelStroke)

            VStack(alignment: .leading, spacing: 8) {
                Text("Foliage")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)

                Toggle(isOn: Binding(
                    get: { windEnabled },
                    set: { newVal in
                        windEnabled = newVal
                        sendScalar(property: "windEnabled", value: newVal ? 1.0 : 0.0)
                    })) {
                    Text("Wind")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                }

                SliderRow(title: "Wind Strength", value: $windStrength, range: 0...2, step: 0.01, tint: .green) { newVal in
                    sendScalar(property: "windStrength", value: newVal)
                }
                SliderRow(title: "Wind Speed", value: $windSpeed, range: 0...5, step: 0.01, tint: .green) { newVal in
                    sendScalar(property: "windSpeed", value: newVal)
                }
                SliderRow(title: "Wind Scale", value: $windScale, range: 0...2, step: 0.01, tint: .green) { newVal in
                    sendScalar(property: "windScale", value: newVal)
                }
                SliderRow(title: "Wind Gust", value: $windGust, range: 0...2, step: 0.01, tint: .green) { newVal in
                    sendScalar(property: "windGust", value: newVal)
                }

                Vector3InputRow(title: "Wind Direction", values: $windDirection) {
                    sendScalar(property: "windDirX", value: windDirection[safe: 0] ?? 0)
                    sendScalar(property: "windDirY", value: windDirection[safe: 1] ?? 0)
                    sendScalar(property: "windDirZ", value: windDirection[safe: 2] ?? 0)
                }

                Toggle(isOn: Binding(
                    get: { lodFadeEnabled },
                    set: { newVal in
                        lodFadeEnabled = newVal
                        sendScalar(property: "lodFadeEnabled", value: newVal ? 1.0 : 0.0)
                    })) {
                    Text("Distance Fade (LOD)")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                }

                Button("Auto LOD (Bounds)") {
                    if let info = CrescentEngineBridge.shared().getFoliageAutoLod(forEntity: entityUUID) as? [String: NSNumber] {
                        lodFadeStart = info["lodFadeStart"]?.floatValue ?? lodFadeStart
                        lodFadeEnd = info["lodFadeEnd"]?.floatValue ?? lodFadeEnd
                        billboardStart = info["billboardStart"]?.floatValue ?? billboardStart
                        billboardEnd = info["billboardEnd"]?.floatValue ?? billboardEnd
                        sendScalar(property: "lodFadeStart", value: lodFadeStart)
                        sendScalar(property: "lodFadeEnd", value: lodFadeEnd)
                        sendScalar(property: "billboardStart", value: billboardStart)
                        sendScalar(property: "billboardEnd", value: billboardEnd)
                    }
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 11, weight: .semibold))

                if lodFadeEnabled {
                    SliderRow(title: "Fade Start", value: $lodFadeStart, range: 0...200, step: 0.5, tint: .yellow) { newVal in
                        sendScalar(property: "lodFadeStart", value: newVal)
                    }
                    SliderRow(title: "Fade End", value: $lodFadeEnd, range: 0...400, step: 0.5, tint: .yellow) { newVal in
                        sendScalar(property: "lodFadeEnd", value: newVal)
                    }
                }

                Toggle(isOn: Binding(
                    get: { ditherEnabled },
                    set: { newVal in
                        ditherEnabled = newVal
                        sendScalar(property: "ditherEnabled", value: newVal ? 1.0 : 0.0)
                    })) {
                    Text("Dither Crossfade")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                }

                Toggle(isOn: Binding(
                    get: { billboardEnabled },
                    set: { newVal in
                        billboardEnabled = newVal
                        sendScalar(property: "billboardEnabled", value: newVal ? 1.0 : 0.0)
                    })) {
                    Text("Billboard LOD")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                }

                if billboardEnabled {
                    SliderRow(title: "Billboard Start", value: $billboardStart, range: 0...500, step: 1.0, tint: .orange) { newVal in
                        sendScalar(property: "billboardStart", value: newVal)
                    }
                    SliderRow(title: "Billboard End", value: $billboardEnd, range: 0...600, step: 1.0, tint: .orange) { newVal in
                        sendScalar(property: "billboardEnd", value: newVal)
                    }
                }

                Toggle(isOn: Binding(
                    get: { impostorEnabled },
                    set: { newVal in
                        impostorEnabled = newVal
                        sendScalar(property: "impostorEnabled", value: newVal ? 1.0 : 0.0)
                    })) {
                    Text("Impostor Atlas")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                }

                if impostorEnabled {
                    SliderRow(title: "Atlas Rows", value: Binding(
                        get: { impostorRows },
                        set: { newVal in
                            impostorRows = newVal
                            sendScalar(property: "impostorRows", value: newVal)
                        }), range: 1...16, step: 1, tint: .orange, onChange: { _ in })

                    SliderRow(title: "Atlas Cols", value: Binding(
                        get: { impostorCols },
                        set: { newVal in
                            impostorCols = newVal
                            sendScalar(property: "impostorCols", value: newVal)
                        }), range: 1...16, step: 1, tint: .orange, onChange: { _ in })

                    Button("Bake Impostor Atlas") {
                        _ = CrescentEngineBridge.shared().bakeImpostorAtlas(forEntity: entityUUID,
                                                                            rows: Int(impostorRows),
                                                                            cols: Int(impostorCols),
                                                                            tileSize: 256)
                    }
                    .buttonStyle(.bordered)
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                }
            }
            
            Divider()
                .overlay(EditorTheme.panelStroke)
            
            VStack(alignment: .leading, spacing: 8) {
                Text("UV Controls")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                
                SliderRow(title: "Tiling X", value: Binding(
                    get: { tiling[safe: 0] ?? 1 },
                    set: { newVal in
                        if tiling.count > 0 { tiling[0] = newVal }
                        sendScalar(property: "tilingX", value: newVal)
                    }), range: 0.1...10, step: 0.1, tint: .blue, onChange: { _ in })
                
                SliderRow(title: "Tiling Y", value: Binding(
                    get: { tiling[safe: 1] ?? 1 },
                    set: { newVal in
                        if tiling.count > 1 { tiling[1] = newVal }
                        sendScalar(property: "tilingY", value: newVal)
                    }), range: 0.1...10, step: 0.1, tint: .blue, onChange: { _ in })
                
                SliderRow(title: "Offset X", value: Binding(
                    get: { offset[safe: 0] ?? 0 },
                    set: { newVal in
                        if offset.count > 0 { offset[0] = newVal }
                        sendScalar(property: "offsetX", value: newVal)
                    }), range: -5...5, step: 0.05, tint: .gray, onChange: { _ in })
                
                SliderRow(title: "Offset Y", value: Binding(
                    get: { offset[safe: 1] ?? 0 },
                    set: { newVal in
                        if offset.count > 1 { offset[1] = newVal }
                        sendScalar(property: "offsetY", value: newVal)
                    }), range: -5...5, step: 0.05, tint: .gray, onChange: { _ in })
            }
            
            Divider()
                .overlay(EditorTheme.panelStroke)
            
            VStack(alignment: .leading, spacing: 8) {
                Text("Texture Maps")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)
                
                ForEach(TextureSlot.allCases, id: \.self) { slot in
                    TextureSlotRow(
                        title: slot.displayName,
                        path: texturePaths[slot.rawValue] ?? ""
                    ) {
                        activeTextureSlot = slot
                        showFileImporter = true
                    } onDrop: { url in
                        handleTextureDrop(url, slot: slot)
                    } onClear: {
                        let bridge = CrescentEngineBridge.shared()
                        for uuid in targetUUIDs() {
                            if applyToAllMaterials {
                                bridge.clearTextureForEntityAllMaterials(uuid, slot: slot.rawValue)
                            } else {
                                bridge.clearTexture(forEntity: uuid, slot: slot.rawValue)
                            }
                        }
                        refreshMaterial()
                    }
                }
            }
        }
        .onAppear {
            refreshMaterial()
            applyToAllSelected = selectedUUIDs.count > 1
        }
        .onChange(of: selectedUUIDs) { newSelection in
            if newSelection.count > 1 {
                applyToAllSelected = true
            } else {
                applyToAllSelected = false
            }
        }
        .onReceive(timer) { _ in
            refreshMaterial()
        }
        .fileImporter(isPresented: $showFileImporter, allowedContentTypes: [.image]) { result in
            handleTextureImport(result)
        }
    }
    
    private func refreshMaterial() {
        let bridge = CrescentEngineBridge.shared()
        guard let info = bridge.getMaterialInfo(forEntity: entityUUID) as? [String: Any] else { return }
        
        if let albedoVals = info["albedo"] as? [NSNumber], albedoVals.count >= 4 {
            albedo = albedoVals.map { $0.floatValue }
        }
        if let emissionVals = info["emission"] as? [NSNumber], emissionVals.count >= 3 {
            emission = emissionVals.map { $0.floatValue }
        }
        
        metallic = (info["metallic"] as? NSNumber)?.floatValue ?? metallic
        roughness = (info["roughness"] as? NSNumber)?.floatValue ?? roughness
        ao = (info["ao"] as? NSNumber)?.floatValue ?? ao
        emissionStrength = (info["emissionStrength"] as? NSNumber)?.floatValue ?? emissionStrength
        normalScale = (info["normalScale"] as? NSNumber)?.floatValue ?? normalScale
        heightScale = (info["heightScale"] as? NSNumber)?.floatValue ?? heightScale
        if let invertVal = info["heightInvert"] as? NSNumber {
            heightInvert = invertVal.intValue != 0
        }
        if let modeVal = info["renderMode"] as? NSNumber {
            renderMode = modeVal.intValue
        }
        if let cutoffVal = info["alphaCutoff"] as? NSNumber {
            alphaCutoff = cutoffVal.floatValue
        }
        if let twoSidedVal = info["twoSided"] as? NSNumber {
            twoSided = twoSidedVal.intValue != 0
        }
        if let a2cVal = info["alphaToCoverage"] as? NSNumber {
            alphaToCoverage = a2cVal.intValue != 0
        }
        if let windVal = info["windEnabled"] as? NSNumber {
            windEnabled = windVal.intValue != 0
        }
        windStrength = (info["windStrength"] as? NSNumber)?.floatValue ?? windStrength
        windSpeed = (info["windSpeed"] as? NSNumber)?.floatValue ?? windSpeed
        windScale = (info["windScale"] as? NSNumber)?.floatValue ?? windScale
        windGust = (info["windGust"] as? NSNumber)?.floatValue ?? windGust
        if let windDirVals = info["windDirection"] as? [NSNumber], windDirVals.count >= 3 {
            windDirection = windDirVals.map { $0.floatValue }
        }
        if let lodEnabled = info["lodFadeEnabled"] as? NSNumber {
            lodFadeEnabled = lodEnabled.intValue != 0
        }
        lodFadeStart = (info["lodFadeStart"] as? NSNumber)?.floatValue ?? lodFadeStart
        lodFadeEnd = (info["lodFadeEnd"] as? NSNumber)?.floatValue ?? lodFadeEnd
        if let ditherVal = info["ditherEnabled"] as? NSNumber {
            ditherEnabled = ditherVal.intValue != 0
        }
        if let billboardVal = info["billboardEnabled"] as? NSNumber {
            billboardEnabled = billboardVal.intValue != 0
        }
        billboardStart = (info["billboardStart"] as? NSNumber)?.floatValue ?? billboardStart
        billboardEnd = (info["billboardEnd"] as? NSNumber)?.floatValue ?? billboardEnd
        if let impostorVal = info["impostorEnabled"] as? NSNumber {
            impostorEnabled = impostorVal.intValue != 0
        }
        impostorRows = (info["impostorRows"] as? NSNumber)?.floatValue ?? impostorRows
        impostorCols = (info["impostorCols"] as? NSNumber)?.floatValue ?? impostorCols
        
        if let tilingVals = info["tiling"] as? [NSNumber], tilingVals.count >= 2 {
            tiling = tilingVals.map { $0.floatValue }
        }
        if let offsetVals = info["offset"] as? [NSNumber], offsetVals.count >= 2 {
            offset = offsetVals.map { $0.floatValue }
        }
        
        if let textures = info["textures"] as? [String: String] {
            texturePaths = textures
        } else if let textures = info["textures"] as? [String: Any] {
            var mapped: [String: String] = [:]
            for (key, value) in textures {
                if let k = key as? String, let v = value as? String {
                    mapped[k] = v
                }
            }
            texturePaths = mapped
        }
    }

    private func targetUUIDs() -> [String] {
        if applyToAllSelected && selectedUUIDs.count > 1 {
            return Array(selectedUUIDs)
        }
        return [entityUUID]
    }
    
    private func colorFrom(_ values: [Float]) -> Color {
        let r = Double(values[safe: 0] ?? 1)
        let g = Double(values[safe: 1] ?? 1)
        let b = Double(values[safe: 2] ?? 1)
        let a = Double(values[safe: 3] ?? 1)
        return Color(red: r, green: g, blue: b, opacity: a)
    }
    
    private func updateAlbedoColor(_ color: Color) {
        guard let components = color.nsColorComponents else { return }
        albedo = [Float(components.r), Float(components.g), Float(components.b), Float(components.a)]
        let bridge = CrescentEngineBridge.shared()
        for uuid in targetUUIDs() {
            if applyToAllMaterials {
                bridge.setMaterialColorForEntityAllMaterials(uuid, property: "albedo", r: Float(components.r), g: Float(components.g), b: Float(components.b), a: Float(components.a))
            } else {
                bridge.setMaterialColorForEntity(uuid, property: "albedo", r: Float(components.r), g: Float(components.g), b: Float(components.b), a: Float(components.a))
            }
        }
    }
    
    private func updateEmissionColor(_ color: Color) {
        guard let components = color.nsColorComponents else { return }
        emission = [Float(components.r), Float(components.g), Float(components.b)]
        let bridge = CrescentEngineBridge.shared()
        for uuid in targetUUIDs() {
            if applyToAllMaterials {
                bridge.setMaterialColorForEntityAllMaterials(uuid, property: "emission", r: Float(components.r), g: Float(components.g), b: Float(components.b), a: 1.0)
            } else {
                bridge.setMaterialColorForEntity(uuid, property: "emission", r: Float(components.r), g: Float(components.g), b: Float(components.b), a: 1.0)
            }
        }
    }
    
    private func sendScalar(property: String, value: Float) {
        let bridge = CrescentEngineBridge.shared()
        for uuid in targetUUIDs() {
            if applyToAllMaterials {
                bridge.setMaterialScalarForEntityAllMaterials(uuid, property: property, value: value)
            } else {
                bridge.setMaterialScalarForEntity(uuid, property: property, value: value)
            }
        }
    }
    
    private func handleTextureImport(_ result: Result<URL, Error>) {
        guard let slot = activeTextureSlot else { return }
        
        switch result {
        case .success(let url):
            applyTextureURL(url, slot: slot)
        case .failure(let error):
            print("Texture import failed: \(error.localizedDescription)")
        }
        
        activeTextureSlot = nil
    }

    private func handleTextureDrop(_ url: URL, slot: TextureSlot) {
        applyTextureURL(url, slot: slot)
    }

    private func applyTextureURL(_ url: URL, slot: TextureSlot) {
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
        
        let ext = resolvedURL.pathExtension.lowercased()
        if !textureExtensions.contains(ext) {
            print("Unsupported texture type: \(resolvedURL.lastPathComponent)")
            return
        }
        
        var path = resolvedURL.path
        if !FileManager.default.fileExists(atPath: path) {
            print("Texture not found at path: \(path)")
            return
        }

        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "texture")
        if !imported.isEmpty {
            path = imported
        }

        let bridge = CrescentEngineBridge.shared()
        var primaryOk = false
        for uuid in targetUUIDs() {
            let ok: Bool
            if applyToAllMaterials {
                ok = bridge.loadTextureForEntityAllMaterials(uuid, slot: slot.rawValue, path: path)
            } else {
                ok = bridge.loadTexture(forEntity: uuid, slot: slot.rawValue, path: path)
            }
            if uuid == entityUUID {
                primaryOk = ok
            }
        }

        if primaryOk {
            texturePaths[slot.rawValue] = URL(fileURLWithPath: path).lastPathComponent
            refreshMaterial()
        } else {
            print("Engine failed to load texture at path: \(path)")
        }
    }
}

struct HLODInspectorSection: View {
    let uuids: [String]
    @State private var lastProxyUUID: String = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Button("Build HLOD From Selection") {
                let proxy = CrescentEngineBridge.shared().buildHLOD(from: uuids)
                if !proxy.isEmpty {
                    lastProxyUUID = proxy
                }
            }
            .buttonStyle(.bordered)
            .font(EditorTheme.font(size: 11, weight: .semibold))

            if !lastProxyUUID.isEmpty {
                Text("Created HLOD Proxy: \(lastProxyUUID)")
                    .font(EditorTheme.font(size: 10, weight: .regular))
                    .foregroundColor(EditorTheme.textMuted)
            }
        }
    }
}

struct SliderRow: View {
    let title: String
    @Binding var value: Float
    var range: ClosedRange<Float> = 0...1
    var step: Float = 0.01
    var tint: Color = .accentColor
    var onChange: ((Float) -> Void)?
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                    .font(EditorTheme.font(size: 11, weight: .medium))
                Spacer()
                Text(String(format: "%.2f", value))
                    .font(EditorTheme.mono(size: 10))
                    .foregroundColor(EditorTheme.textMuted)
            }
            Slider(
                value: Binding(
                    get: { Double(value) },
                    set: { newVal in
                        value = Float(newVal)
                        onChange?(Float(newVal))
                    }),
                in: Double(range.lowerBound)...Double(range.upperBound),
                step: Double(step)
            )
            .accentColor(tint)
        }
    }
}

// MARK: - Decal Inspector

private enum DecalTextureSlot: String, CaseIterable {
    case albedo
    case normal
    case orm
    case mask

    var displayName: String {
        switch self {
        case .albedo: return "Albedo / Base Color"
        case .normal: return "Normal Map"
        case .orm: return "ORM (AO/Roughness/Metallic)"
        case .mask: return "Mask / Opacity"
        }
    }
}

struct DecalInspector: View {
    let entityUUID: String

    @State private var tint: [Float] = [1, 1, 1, 1]
    @State private var opacity: Float = 1.0
    @State private var softness: Float = 0.05
    @State private var tiling: [Float] = [1, 1]
    @State private var offset: [Float] = [0, 0]
    @State private var texturePaths: [String: String] = [:]

    @State private var activeSlot: DecalTextureSlot?
    @State private var showFileImporter: Bool = false

    private let timer = Timer.publish(every: 0.75, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ColorPicker("Tint", selection: Binding(
                get: { colorFrom(tint) },
                set: { updateTintColor($0) }
            ), supportsOpacity: false)
            .font(EditorTheme.font(size: 11, weight: .medium))

            SliderRow(title: "Opacity", value: $opacity, range: 0...1, step: 0.01, tint: .teal) { newVal in
                setDecalInfo(["opacity": newVal])
            }

            SliderRow(title: "Edge Softness", value: $softness, range: 0...0.4, step: 0.01, tint: .blue) { newVal in
                setDecalInfo(["softness": newVal])
            }

            Divider()
                .overlay(EditorTheme.panelStroke)

            VStack(alignment: .leading, spacing: 8) {
                Text("UV Controls")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)

                SliderRow(title: "Tiling X", value: Binding(
                    get: { tiling[safe: 0] ?? 1 },
                    set: { newVal in
                        if tiling.count > 0 { tiling[0] = newVal }
                        setDecalInfo(["tiling": tiling])
                    }), range: 0.1...10, step: 0.1, tint: .blue, onChange: { _ in })

                SliderRow(title: "Tiling Y", value: Binding(
                    get: { tiling[safe: 1] ?? 1 },
                    set: { newVal in
                        if tiling.count > 1 { tiling[1] = newVal }
                        setDecalInfo(["tiling": tiling])
                    }), range: 0.1...10, step: 0.1, tint: .blue, onChange: { _ in })

                SliderRow(title: "Offset X", value: Binding(
                    get: { offset[safe: 0] ?? 0 },
                    set: { newVal in
                        if offset.count > 0 { offset[0] = newVal }
                        setDecalInfo(["offset": offset])
                    }), range: -5...5, step: 0.05, tint: .gray, onChange: { _ in })

                SliderRow(title: "Offset Y", value: Binding(
                    get: { offset[safe: 1] ?? 0 },
                    set: { newVal in
                        if offset.count > 1 { offset[1] = newVal }
                        setDecalInfo(["offset": offset])
                    }), range: -5...5, step: 0.05, tint: .gray, onChange: { _ in })
            }

            Divider()
                .overlay(EditorTheme.panelStroke)

            VStack(alignment: .leading, spacing: 8) {
                Text("Decal Maps")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textPrimary)

                ForEach(DecalTextureSlot.allCases, id: \.self) { slot in
                    TextureSlotRow(
                        title: slot.displayName,
                        path: texturePaths[slot.rawValue] ?? ""
                    ) {
                        activeSlot = slot
                        showFileImporter = true
                    } onDrop: { url in
                        applyTextureURL(url, slot: slot)
                    } onClear: {
                        CrescentEngineBridge.shared().clearDecalTexture(entityUUID, slot: slot.rawValue)
                        refreshDecal()
                    }
                }
            }
        }
        .onAppear {
            refreshDecal()
        }
        .onReceive(timer) { _ in
            refreshDecal()
        }
        .fileImporter(isPresented: $showFileImporter, allowedContentTypes: [.image]) { result in
            handleTextureImport(result)
        }
    }

    private func refreshDecal() {
        let bridge = CrescentEngineBridge.shared()
        guard let info = bridge.getDecalInfo(entityUUID) as? [String: Any] else { return }

        texturePaths = [:]
        if let colorVals = info["color"] as? [NSNumber], colorVals.count >= 4 {
            tint = colorVals.map { $0.floatValue }
        }
        if let opacityVal = info["opacity"] as? NSNumber {
            opacity = opacityVal.floatValue
        }
        if let softnessVal = info["softness"] as? NSNumber {
            softness = softnessVal.floatValue
        }
        if let tilingVals = info["tiling"] as? [NSNumber], tilingVals.count >= 2 {
            tiling = tilingVals.map { $0.floatValue }
        }
        if let offsetVals = info["offset"] as? [NSNumber], offsetVals.count >= 2 {
            offset = offsetVals.map { $0.floatValue }
        }
        if let albedo = info["albedo"] as? String {
            texturePaths["albedo"] = albedo
        }
        if let normal = info["normal"] as? String {
            texturePaths["normal"] = normal
        }
        if let orm = info["orm"] as? String {
            texturePaths["orm"] = orm
        }
        if let mask = info["mask"] as? String {
            texturePaths["mask"] = mask
        }
    }

    private func colorFrom(_ values: [Float]) -> Color {
        let r = Double(values[safe: 0] ?? 1)
        let g = Double(values[safe: 1] ?? 1)
        let b = Double(values[safe: 2] ?? 1)
        let a = Double(values[safe: 3] ?? 1)
        return Color(red: r, green: g, blue: b, opacity: a)
    }

    private func updateTintColor(_ color: Color) {
        guard let components = color.nsColorComponents else { return }
        tint = [Float(components.r), Float(components.g), Float(components.b), 1.0]
        setDecalInfo(["color": tint])
    }

    private func setDecalInfo(_ updates: [String: Any]) {
        CrescentEngineBridge.shared().setDecalInfo(entityUUID, info: updates)
    }

    private func handleTextureImport(_ result: Result<URL, Error>) {
        guard let slot = activeSlot else { return }
        switch result {
        case .success(let url):
            applyTextureURL(url, slot: slot)
        case .failure(let error):
            print("Decal texture import failed: \(error.localizedDescription)")
        }
        activeSlot = nil
    }

    private func applyTextureURL(_ url: URL, slot: DecalTextureSlot) {
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
        var path = resolvedURL.path
        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "texture")
        if !imported.isEmpty {
            path = imported
        }
        let ok = CrescentEngineBridge.shared().loadDecalTexture(entityUUID, slot: slot.rawValue, path: path)
        if ok {
            refreshDecal()
        } else {
            print("Engine failed to load decal texture at path: \(path)")
        }
    }
}

struct TextureSlotRow: View {
    let title: String
    let path: String
    let onLoad: () -> Void
    let onDrop: (URL) -> Void
    let onClear: () -> Void
    @State private var isDropping: Bool = false
    
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(title)
                    .font(EditorTheme.font(size: 11, weight: .medium))
                Spacer()
                Button("Load") { onLoad() }
                    .buttonStyle(.borderless)
                Button("Clear") { onClear() }
                    .buttonStyle(.borderless)
                    .foregroundColor(.red.opacity(0.9))
            }
            Text(path.isEmpty ? "No texture assigned" : path)
                .font(EditorTheme.mono(size: 10))
                .foregroundColor(path.isEmpty ? EditorTheme.textMuted : EditorTheme.textPrimary)
                .lineLimit(1)
                .truncationMode(.middle)
        }
        .padding(8)
        .background(EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(isDropping ? EditorTheme.textAccent : EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(6)
        .onDrop(of: [UTType.fileURL], isTargeted: $isDropping) { providers in
            for provider in providers where provider.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) {
                provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                    DispatchQueue.main.async {
                        if let data = item as? Data,
                           let url = URL(dataRepresentation: data, relativeTo: nil) {
                            onDrop(url)
                        } else if let url = item as? URL {
                            onDrop(url)
                        }
                    }
                }
            }
            return true
        }
    }
}

private extension Color {
    var nsColorComponents: (r: CGFloat, g: CGFloat, b: CGFloat, a: CGFloat)? {
        #if os(macOS)
        let nsColor = NSColor(self)
        guard let rgb = nsColor.usingColorSpace(.deviceRGB) else { return nil }
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        rgb.getRed(&r, green: &g, blue: &b, alpha: &a)
        return (r, g, b, a)
        #else
        return nil
        #endif
    }
}
