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
                    VStack(alignment: .leading, spacing: 14) {
                        InspectorSummaryCard(entityName: selectedEntity.name, selectionCount: editorState.selectedEntityUUIDs.count)
                        
                        ComponentSection(title: "Transform", icon: "arrow.up.and.down.and.arrow.left.and.right") {
                            TransformInspector(selectedUUIDs: editorState.selectedEntityUUIDs)  // Pass ALL selected UUIDs
                        }
                        
                        // Only show Material if this entity isn't a light
                        if lightSection == nil && decalSection == nil {
                            ComponentSection(title: "Material", icon: "paintpalette.fill") {
                                MaterialInspector(entityUUID: selectedEntity.uuid)
                            }
                        }

                        if let physicsView = physicsSection {
                            physicsView
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
    let entityName: String
    let selectionCount: Int
    
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(entityName)
                .font(EditorTheme.font(size: 16, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)
                .lineLimit(2)
            
            Text(selectionCount > 1 ? "\(selectionCount) objects selected" : "GameObject")
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
    @State private var debugDraw: Bool = false

    private let timer = Timer.publish(every: 0.8, on: .main, in: .common).autoconnect()
    private let bodyTypes = ["Static", "Dynamic", "Kinematic"]
    private let shapeTypes = ["Box", "Sphere", "Capsule"]

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
        }
        .onAppear { refresh() }
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
                } else {
                    SliderRow(title: "Radius", value: $radius, range: 0.01...10, step: 0.01) { _ in
                        pushCollider()
                    }
                    SliderRow(title: "Height", value: $height, range: 0.1...20, step: 0.05) { _ in
                        pushCollider()
                    }
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

                SliderRow(title: "Friction", value: $friction, range: 0...2, step: 0.01) { _ in
                    pushCollider()
                }

                SliderRow(title: "Restitution", value: $restitution, range: 0...1, step: 0.01) { _ in
                    pushCollider()
                }
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
        } else {
            hasCollider = false
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
            "restitution": restitution
        ]
        _ = CrescentEngineBridge.shared().setColliderInfo(uuid: entityUUID, info: info)
    }

    private func removeRigidbody() {
        CrescentEngineBridge.shared().removeRigidbody(uuid: entityUUID)
        hasRigidbody = false
    }

    private func removeCollider() {
        CrescentEngineBridge.shared().removeCollider(uuid: entityUUID)
        hasCollider = false
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
    
    @State private var albedo: [Float] = [1, 1, 1, 1]
    @State private var emission: [Float] = [0, 0, 0]
    @State private var metallic: Float = 0
    @State private var roughness: Float = 0.5
    @State private var ao: Float = 1.0
    @State private var emissionStrength: Float = 0.0
    @State private var normalScale: Float = 1.0
    @State private var heightScale: Float = 0.02
    @State private var heightInvert: Bool = false
    @State private var tiling: [Float] = [1, 1]
    @State private var offset: [Float] = [0, 0]
    @State private var texturePaths: [String: String] = [:]
    private let textureExtensions: Set<String> = [
        "png", "jpg", "jpeg", "tga", "bmp", "gif", "tif", "tiff", "ktx", "ktx2", "dds"
    ]
    
    @State private var activeTextureSlot: TextureSlot?
    @State private var showFileImporter: Bool = false
    
    private let timer = Timer.publish(every: 0.75, on: .main, in: .common).autoconnect()
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
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
                        CrescentEngineBridge.shared().clearTexture(forEntity: entityUUID, slot: slot.rawValue)
                        refreshMaterial()
                    }
                }
            }
        }
        .onAppear {
            refreshMaterial()
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
        CrescentEngineBridge.shared().setMaterialColorForEntity(entityUUID, property: "albedo", r: Float(components.r), g: Float(components.g), b: Float(components.b), a: Float(components.a))
    }
    
    private func updateEmissionColor(_ color: Color) {
        guard let components = color.nsColorComponents else { return }
        emission = [Float(components.r), Float(components.g), Float(components.b)]
        CrescentEngineBridge.shared().setMaterialColorForEntity(entityUUID, property: "emission", r: Float(components.r), g: Float(components.g), b: Float(components.b), a: 1.0)
    }
    
    private func sendScalar(property: String, value: Float) {
        CrescentEngineBridge.shared().setMaterialScalarForEntity(entityUUID, property: property, value: value)
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

        let ok = CrescentEngineBridge.shared().loadTexture(forEntity: entityUUID, slot: slot.rawValue, path: path)
        if ok {
            texturePaths[slot.rawValue] = URL(fileURLWithPath: path).lastPathComponent
            refreshMaterial()
        } else {
            print("Engine failed to load texture at path: \(path)")
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
