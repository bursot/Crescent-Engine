import SwiftUI
import Combine

struct IKInspector: View {
    let entityUUID: String

    @State private var hasIK: Bool = false
    @State private var rootBone: String = ""
    @State private var midBone: String = ""
    @State private var endBone: String = ""
    @State private var target: [Float] = [0, 0, 0]
    @State private var targetInWorld: Bool = true
    @State private var weight: Float = 1.0

    private let timer = Timer.publish(every: 0.8, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("IK")
                .font(EditorTheme.font(size: 11, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)

            if !hasIK {
                Button("Add IK Constraint") {
                    addIK()
                }
                .buttonStyle(.borderless)
                .font(EditorTheme.font(size: 11, weight: .semibold))
            } else {
                TextField("Root Bone", text: Binding(
                    get: { rootBone },
                    set: { newVal in
                        rootBone = newVal
                        pushIK()
                    }))
                .textFieldStyle(.roundedBorder)
                .font(EditorTheme.font(size: 10))

                TextField("Mid Bone", text: Binding(
                    get: { midBone },
                    set: { newVal in
                        midBone = newVal
                        pushIK()
                    }))
                .textFieldStyle(.roundedBorder)
                .font(EditorTheme.font(size: 10))

                TextField("End Bone", text: Binding(
                    get: { endBone },
                    set: { newVal in
                        endBone = newVal
                        pushIK()
                    }))
                .textFieldStyle(.roundedBorder)
                .font(EditorTheme.font(size: 10))

                Vector3InputRow(title: "Target", values: $target) {
                    pushIK()
                }

                Toggle("Target In World", isOn: Binding(
                    get: { targetInWorld },
                    set: { newVal in
                        targetInWorld = newVal
                        pushIK()
                    }))
                .font(EditorTheme.font(size: 11, weight: .medium))

                SliderRow(title: "Weight", value: $weight, range: 0...1, step: 0.01, tint: .blue) { newVal in
                    weight = newVal
                    pushIK()
                }
            }
        }
        .onAppear {
            refreshIK()
        }
        .onReceive(timer) { _ in
            refreshIK()
        }
    }

    private func refreshIK() {
        let bridge = CrescentEngineBridge.shared()
        guard let info = bridge.getIKConstraintInfo(uuid: entityUUID) as? [String: Any], !info.isEmpty else {
            hasIK = false
            return
        }
        hasIK = true
        rootBone = info["root"] as? String ?? rootBone
        midBone = info["mid"] as? String ?? midBone
        endBone = info["end"] as? String ?? endBone
        if let targetValues = info["target"] as? [NSNumber], targetValues.count >= 3 {
            target = targetValues.prefix(3).map { $0.floatValue }
        }
        targetInWorld = (info["world"] as? NSNumber)?.boolValue ?? targetInWorld
        weight = (info["weight"] as? NSNumber)?.floatValue ?? weight
    }

    private func pushIK() {
        let info: [String: Any] = [
            "root": rootBone,
            "mid": midBone,
            "end": endBone,
            "target": target,
            "world": targetInWorld,
            "weight": weight
        ]
        _ = CrescentEngineBridge.shared().setIKConstraintInfo(uuid: entityUUID, info: info)
    }

    private func addIK() {
        let info: [String: Any] = [
            "root": rootBone,
            "mid": midBone,
            "end": endBone,
            "target": target,
            "world": targetInWorld,
            "weight": weight
        ]
        _ = CrescentEngineBridge.shared().setIKConstraintInfo(uuid: entityUUID, info: info)
        refreshIK()
    }
}

struct AnimatorGraphEditor: View {
    let entityUUID: String
    let clipNames: [String]

    private struct GraphParameter: Identifiable {
        let id = UUID()
        var name: String
        var type: String
        var floatValue: Float
        var intValue: Int
        var boolValue: Bool
    }

    private struct GraphState: Identifiable {
        let id = UUID()
        var name: String
        var type: String
        var clipIndex: Int
        var blendTreeIndex: Int
        var speed: Float
        var loop: Bool
    }

    private struct GraphBlendMotion: Identifiable {
        let id = UUID()
        var clipIndex: Int
        var threshold: Float
    }

    private struct GraphBlendTree: Identifiable {
        let id = UUID()
        var name: String
        var type: String
        var parameter: String
        var motions: [GraphBlendMotion]
    }

    private struct GraphCondition: Identifiable {
        let id = UUID()
        var parameter: String
        var op: String
        var threshold: Float
        var intThreshold: Int
        var boolThreshold: Bool
    }

    private struct GraphTransition: Identifiable {
        let id = UUID()
        var fromState: Int
        var toState: Int
        var duration: Float
        var hasExitTime: Bool
        var exitTime: Float
        var fixedDuration: Bool
        var conditions: [GraphCondition]
    }

    @Environment(\.dismiss) private var dismiss
    @State private var parameters: [GraphParameter] = []
    @State private var states: [GraphState] = []
    @State private var blendTrees: [GraphBlendTree] = []
    @State private var transitions: [GraphTransition] = []
    @State private var loaded: Bool = false
    @State private var nodePositions: [UUID: CGPoint] = [:]
    @State private var didInitPositions: Bool = false

    private let paramTypes = ["Float", "Int", "Bool", "Trigger"]
    private let stateTypes = ["Clip", "BlendTree"]
    private let blendTreeTypes = ["Blend1D"]
    private let conditionOps = ["IfTrue", "IfFalse", "Greater", "Less", "GreaterEqual", "LessEqual", "Equal", "NotEqual"]

    var body: some View {
        VStack(spacing: 12) {
            HStack {
                Text("Animator Graph")
                    .font(EditorTheme.font(size: 14, weight: .semibold))
                Spacer()
                Button("Apply") {
                    applyGraph()
                }
                .buttonStyle(.borderless)
                Button("Close") {
                    dismiss()
                }
                .buttonStyle(.borderless)
            }

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    sectionHeader("Graph")
                    graphCanvas

                    sectionHeader("Parameters")
                    parameterEditor

                    sectionHeader("Blend Trees")
                    blendTreeEditor

                    sectionHeader("States")
                    stateEditor

                    sectionHeader("Transitions")
                    transitionEditor
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(16)
        .frame(minWidth: 780, minHeight: 680)
        .background(EditorTheme.panelBackground)
        .onAppear {
            if !loaded {
                loadGraph()
                loaded = true
            }
        }
    }

    private var parameterEditor: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(parameters.indices, id: \.self) { idx in
                HStack(spacing: 8) {
                    TextField("Name", text: Binding(
                        get: { parameters[idx].name },
                        set: { parameters[idx].name = $0 }
                    ))
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 140)

                    Picker("Type", selection: Binding(
                        get: { parameters[idx].type },
                        set: { parameters[idx].type = $0 }
                    )) {
                        ForEach(paramTypes, id: \.self) { type in
                            Text(type).tag(type)
                        }
                    }
                    .pickerStyle(.menu)
                    .frame(width: 110)

                    switch parameters[idx].type {
                    case "Int":
                        TextField("Int", value: Binding(
                            get: { parameters[idx].intValue },
                            set: { parameters[idx].intValue = $0 }
                        ), formatter: NumberFormatter.intFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 80)
                    case "Bool":
                        Toggle("On", isOn: Binding(
                            get: { parameters[idx].boolValue },
                            set: { parameters[idx].boolValue = $0 }
                        ))
                        .labelsHidden()
                    case "Trigger":
                        Text("Trigger")
                            .font(EditorTheme.font(size: 10))
                            .foregroundColor(EditorTheme.textMuted)
                    default:
                        TextField("Float", value: Binding(
                            get: { parameters[idx].floatValue },
                            set: { parameters[idx].floatValue = $0 }
                        ), formatter: NumberFormatter.floatFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 80)
                    }

                    Spacer()
                    Button(action: {
                        parameters.remove(at: idx)
                    }) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            Button("Add Parameter") {
                parameters.append(GraphParameter(name: "", type: "Float", floatValue: 0, intValue: 0, boolValue: false))
            }
            .buttonStyle(.borderless)
            .font(EditorTheme.font(size: 11, weight: .semibold))
        }
        .padding(10)
        .background(EditorTheme.surface)
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(EditorTheme.panelStroke, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    private var graphCanvas: some View {
        GeometryReader { geo in
            ZStack {
                RoundedRectangle(cornerRadius: 10)
                    .fill(EditorTheme.surface)
                    .overlay(
                        RoundedRectangle(cornerRadius: 10)
                            .stroke(EditorTheme.panelStroke, lineWidth: 1)
                    )

                ForEach(transitions) { transition in
                    if let fromPoint = nodePoint(for: transition.fromState, in: geo.size),
                       let toPoint = nodePoint(for: transition.toState, in: geo.size) {
                        GraphLine(from: fromPoint, to: toPoint)
                            .stroke(Color.white.opacity(0.25), style: StrokeStyle(lineWidth: 1, dash: [4, 4]))
                    } else if transition.fromState == -1,
                              let toPoint = nodePoint(for: transition.toState, in: geo.size) {
                        let start = CGPoint(x: 24, y: toPoint.y)
                        GraphLine(from: start, to: toPoint)
                            .stroke(Color.white.opacity(0.2), style: StrokeStyle(lineWidth: 1, dash: [3, 3]))
                    }
                }

                ForEach(states) { state in
                    GraphNode(
                        title: state.name.isEmpty ? "State" : state.name,
                        subtitle: state.type == "BlendTree" ? "BlendTree" : "Clip",
                        isBlendTree: state.type == "BlendTree",
                        position: nodePositions[state.id] ?? defaultPosition(for: state, in: geo.size)
                    )
                    .onAppear {
                        initializePositionsIfNeeded(in: geo.size)
                    }
                    .gesture(
                        DragGesture()
                            .onChanged { value in
                                nodePositions[state.id] = value.location
                            }
                    )
                }
            }
        }
        .frame(height: 320)
    }

    private var blendTreeEditor: some View {
        VStack(alignment: .leading, spacing: 12) {
            ForEach(blendTrees.indices, id: \.self) { idx in
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 8) {
                        TextField("Name", text: Binding(
                            get: { blendTrees[idx].name },
                            set: { blendTrees[idx].name = $0 }
                        ))
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 160)

                        Picker("Type", selection: Binding(
                            get: { blendTrees[idx].type },
                            set: { blendTrees[idx].type = $0 }
                        )) {
                            ForEach(blendTreeTypes, id: \.self) { type in
                                Text(type).tag(type)
                            }
                        }
                        .pickerStyle(.menu)
                        .frame(width: 120)

                        TextField("Parameter", text: Binding(
                            get: { blendTrees[idx].parameter },
                            set: { blendTrees[idx].parameter = $0 }
                        ))
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 140)

                        Spacer()
                        Button(action: {
                            blendTrees.remove(at: idx)
                        }) {
                            Image(systemName: "trash")
                        }
                        .buttonStyle(.borderless)
                    }

                    VStack(alignment: .leading, spacing: 6) {
                        ForEach(blendTrees[idx].motions.indices, id: \.self) { motionIdx in
                            HStack(spacing: 8) {
                                Picker("Clip", selection: Binding(
                                    get: { blendTrees[idx].motions[motionIdx].clipIndex },
                                    set: { blendTrees[idx].motions[motionIdx].clipIndex = $0 }
                                )) {
                                    ForEach(clipOptions, id: \.0) { entry in
                                        Text(entry.1).tag(entry.0)
                                    }
                                }
                                .pickerStyle(.menu)
                                .frame(width: 200)

                                TextField("Threshold", value: Binding(
                                    get: { blendTrees[idx].motions[motionIdx].threshold },
                                    set: { blendTrees[idx].motions[motionIdx].threshold = $0 }
                                ), formatter: NumberFormatter.floatFormatter)
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 90)

                                Button(action: {
                                    blendTrees[idx].motions.remove(at: motionIdx)
                                }) {
                                    Image(systemName: "trash")
                                }
                                .buttonStyle(.borderless)
                            }
                        }
                        Button("Add Motion") {
                            blendTrees[idx].motions.append(GraphBlendMotion(clipIndex: clipOptions.first?.0 ?? -1, threshold: 0))
                        }
                        .buttonStyle(.borderless)
                        .font(EditorTheme.font(size: 10, weight: .semibold))
                    }
                }
                .padding(10)
                .background(EditorTheme.surface)
                .overlay(RoundedRectangle(cornerRadius: 8).stroke(EditorTheme.panelStroke, lineWidth: 1))
                .clipShape(RoundedRectangle(cornerRadius: 8))
            }

            Button("Add Blend Tree") {
                blendTrees.append(GraphBlendTree(name: "", type: "Blend1D", parameter: "", motions: []))
            }
            .buttonStyle(.borderless)
            .font(EditorTheme.font(size: 11, weight: .semibold))
        }
    }

    private var stateEditor: some View {
        VStack(alignment: .leading, spacing: 12) {
            ForEach(states.indices, id: \.self) { idx in
                HStack(spacing: 8) {
                    TextField("Name", text: Binding(
                        get: { states[idx].name },
                        set: { states[idx].name = $0 }
                    ))
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 160)

                    Picker("Type", selection: Binding(
                        get: { states[idx].type },
                        set: { states[idx].type = $0 }
                    )) {
                        ForEach(stateTypes, id: \.self) { type in
                            Text(type).tag(type)
                        }
                    }
                    .pickerStyle(.menu)
                    .frame(width: 120)

                    if states[idx].type == "BlendTree" {
                        Picker("Blend Tree", selection: Binding(
                            get: { states[idx].blendTreeIndex },
                            set: { states[idx].blendTreeIndex = $0 }
                        )) {
                            ForEach(blendTreeOptions, id: \.0) { entry in
                                Text(entry.1).tag(entry.0)
                            }
                        }
                        .pickerStyle(.menu)
                        .frame(width: 200)
                    } else {
                        Picker("Clip", selection: Binding(
                            get: { states[idx].clipIndex },
                            set: { states[idx].clipIndex = $0 }
                        )) {
                            ForEach(clipOptions, id: \.0) { entry in
                                Text(entry.1).tag(entry.0)
                            }
                        }
                        .pickerStyle(.menu)
                        .frame(width: 200)
                    }

                    TextField("Speed", value: Binding(
                        get: { states[idx].speed },
                        set: { states[idx].speed = $0 }
                    ), formatter: NumberFormatter.floatFormatter)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 80)

                    Toggle("Loop", isOn: Binding(
                        get: { states[idx].loop },
                        set: { states[idx].loop = $0 }
                    ))
                    .labelsHidden()

                    Spacer()
                    Button(action: {
                        states.remove(at: idx)
                    }) {
                        Image(systemName: "trash")
                    }
                    .buttonStyle(.borderless)
                }
            }

            Button("Add State") {
                states.append(GraphState(name: "", type: "Clip", clipIndex: clipOptions.first?.0 ?? -1, blendTreeIndex: blendTreeOptions.first?.0 ?? -1, speed: 1.0, loop: true))
            }
            .buttonStyle(.borderless)
            .font(EditorTheme.font(size: 11, weight: .semibold))
        }
        .padding(10)
        .background(EditorTheme.surface)
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(EditorTheme.panelStroke, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    private var transitionEditor: some View {
        VStack(alignment: .leading, spacing: 12) {
            ForEach(transitions.indices, id: \.self) { idx in
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 8) {
                        Picker("From", selection: Binding(
                            get: { transitions[idx].fromState },
                            set: { transitions[idx].fromState = $0 }
                        )) {
                            ForEach(fromStateOptions, id: \.0) { entry in
                                Text(entry.1).tag(entry.0)
                            }
                        }
                        .pickerStyle(.menu)
                        .frame(width: 180)

                        Picker("To", selection: Binding(
                            get: { transitions[idx].toState },
                            set: { transitions[idx].toState = $0 }
                        )) {
                            ForEach(toStateOptions, id: \.0) { entry in
                                Text(entry.1).tag(entry.0)
                            }
                        }
                        .pickerStyle(.menu)
                        .frame(width: 180)

                        TextField("Duration", value: Binding(
                            get: { transitions[idx].duration },
                            set: { transitions[idx].duration = $0 }
                        ), formatter: NumberFormatter.floatFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 90)

                        Toggle("Exit Time", isOn: Binding(
                            get: { transitions[idx].hasExitTime },
                            set: { transitions[idx].hasExitTime = $0 }
                        ))
                        .font(EditorTheme.font(size: 10))

                        TextField("Exit", value: Binding(
                            get: { transitions[idx].exitTime },
                            set: { transitions[idx].exitTime = $0 }
                        ), formatter: NumberFormatter.floatFormatter)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 70)

                        Toggle("Fixed", isOn: Binding(
                            get: { transitions[idx].fixedDuration },
                            set: { transitions[idx].fixedDuration = $0 }
                        ))
                        .font(EditorTheme.font(size: 10))

                        Spacer()
                        Button(action: {
                            transitions.remove(at: idx)
                        }) {
                            Image(systemName: "trash")
                        }
                        .buttonStyle(.borderless)
                    }

                    VStack(alignment: .leading, spacing: 6) {
                        ForEach(transitions[idx].conditions.indices, id: \.self) { condIdx in
                            HStack(spacing: 8) {
                                TextField("Parameter", text: Binding(
                                    get: { transitions[idx].conditions[condIdx].parameter },
                                    set: { transitions[idx].conditions[condIdx].parameter = $0 }
                                ))
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 140)

                                Picker("Op", selection: Binding(
                                    get: { transitions[idx].conditions[condIdx].op },
                                    set: { transitions[idx].conditions[condIdx].op = $0 }
                                )) {
                                    ForEach(conditionOps, id: \.self) { op in
                                        Text(op).tag(op)
                                    }
                                }
                                .pickerStyle(.menu)
                                .frame(width: 140)

                                conditionValueField(for: transitions[idx].conditions[condIdx])

                                Button(action: {
                                    transitions[idx].conditions.remove(at: condIdx)
                                }) {
                                    Image(systemName: "trash")
                                }
                                .buttonStyle(.borderless)
                            }
                        }
                        Button("Add Condition") {
                            let paramName = parameters.first?.name ?? ""
                            transitions[idx].conditions.append(GraphCondition(parameter: paramName, op: "IfTrue", threshold: 0, intThreshold: 0, boolThreshold: false))
                        }
                        .buttonStyle(.borderless)
                        .font(EditorTheme.font(size: 10, weight: .semibold))
                    }
                }
                .padding(10)
                .background(EditorTheme.surface)
                .overlay(RoundedRectangle(cornerRadius: 8).stroke(EditorTheme.panelStroke, lineWidth: 1))
                .clipShape(RoundedRectangle(cornerRadius: 8))
            }

            Button("Add Transition") {
                transitions.append(GraphTransition(fromState: -1, toState: toStateOptions.first?.0 ?? -1, duration: 0.25, hasExitTime: false, exitTime: 0.9, fixedDuration: true, conditions: []))
            }
            .buttonStyle(.borderless)
            .font(EditorTheme.font(size: 11, weight: .semibold))
        }
    }

    private var clipOptions: [(Int, String)] {
        var options: [(Int, String)] = [(-1, "None")]
        for (idx, name) in clipNames.enumerated() {
            options.append((idx, name))
        }
        return options
    }

    private var blendTreeOptions: [(Int, String)] {
        var options: [(Int, String)] = [(-1, "None")]
        for (idx, tree) in blendTrees.enumerated() {
            let name = tree.name.isEmpty ? "BlendTree \(idx)" : tree.name
            options.append((idx, name))
        }
        return options
    }

    private var fromStateOptions: [(Int, String)] {
        var options: [(Int, String)] = [(-1, "Any")]
        for (idx, state) in states.enumerated() {
            let name = state.name.isEmpty ? "State \(idx)" : state.name
            options.append((idx, name))
        }
        return options
    }

    private var toStateOptions: [(Int, String)] {
        var options: [(Int, String)] = []
        for (idx, state) in states.enumerated() {
            let name = state.name.isEmpty ? "State \(idx)" : state.name
            options.append((idx, name))
        }
        return options.isEmpty ? [(-1, "None")] : options
    }

    @ViewBuilder
    private func conditionValueField(for condition: GraphCondition) -> some View {
        let paramType = parameterType(for: condition.parameter)
        switch paramType {
        case "Bool", "Trigger":
            Toggle("Value", isOn: Binding(
                get: { condition.boolThreshold },
                set: { newVal in
                    updateCondition(condition.id) { $0.boolThreshold = newVal }
                }
            ))
            .labelsHidden()
        case "Int":
            TextField("Int", value: Binding(
                get: { condition.intThreshold },
                set: { newVal in
                    updateCondition(condition.id) { $0.intThreshold = newVal }
                }
            ), formatter: NumberFormatter.intFormatter)
            .textFieldStyle(.roundedBorder)
            .frame(width: 80)
        default:
            TextField("Float", value: Binding(
                get: { condition.threshold },
                set: { newVal in
                    updateCondition(condition.id) { $0.threshold = newVal }
                }
            ), formatter: NumberFormatter.floatFormatter)
            .textFieldStyle(.roundedBorder)
            .frame(width: 80)
        }
    }

    private func parameterType(for name: String) -> String {
        parameters.first(where: { $0.name == name })?.type ?? "Float"
    }

    private func updateCondition(_ id: UUID, update: (inout GraphCondition) -> Void) {
        for tIdx in transitions.indices {
            if let cIdx = transitions[tIdx].conditions.firstIndex(where: { $0.id == id }) {
                update(&transitions[tIdx].conditions[cIdx])
                return
            }
        }
    }

    private func sectionHeader(_ title: String) -> some View {
        Text(title)
            .font(EditorTheme.font(size: 11, weight: .semibold))
            .foregroundColor(EditorTheme.textPrimary)
    }

    private func loadGraph() {
        let bridge = CrescentEngineBridge.shared()
        guard let graph = bridge.getAnimatorGraph(uuid: entityUUID) as? [String: Any] else { return }

        didInitPositions = false

        parameters = []
        if let params = graph["parameters"] as? [[String: Any]] {
            for entry in params {
                let name = entry["name"] as? String ?? ""
                let type = entry["type"] as? String ?? "Float"
                let floatValue = (entry["float"] as? NSNumber)?.floatValue ?? 0
                let intValue = (entry["int"] as? NSNumber)?.intValue ?? 0
                let boolValue = (entry["bool"] as? NSNumber)?.boolValue ?? false
                parameters.append(GraphParameter(name: name, type: type, floatValue: floatValue, intValue: intValue, boolValue: boolValue))
            }
        }

        blendTrees = []
        if let trees = graph["blendTrees"] as? [[String: Any]] {
            for entry in trees {
                let name = entry["name"] as? String ?? ""
                let type = entry["type"] as? String ?? "Blend1D"
                let parameter = entry["parameter"] as? String ?? ""
                var motions: [GraphBlendMotion] = []
                if let mlist = entry["motions"] as? [[String: Any]] {
                    for motionEntry in mlist {
                        let clipIndex = (motionEntry["clipIndex"] as? NSNumber)?.intValue ?? -1
                        let threshold = (motionEntry["threshold"] as? NSNumber)?.floatValue ?? 0
                        motions.append(GraphBlendMotion(clipIndex: clipIndex, threshold: threshold))
                    }
                }
                blendTrees.append(GraphBlendTree(name: name, type: type, parameter: parameter, motions: motions))
            }
        }

        states = []
        if let list = graph["states"] as? [[String: Any]] {
            for entry in list {
                let name = entry["name"] as? String ?? ""
                let type = entry["type"] as? String ?? "Clip"
                let clipIndex = (entry["clipIndex"] as? NSNumber)?.intValue ?? -1
                let blendTreeIndex = (entry["blendTree"] as? NSNumber)?.intValue ?? -1
                let speed = (entry["speed"] as? NSNumber)?.floatValue ?? 1
                let loop = (entry["loop"] as? NSNumber)?.boolValue ?? true
                states.append(GraphState(name: name, type: type, clipIndex: clipIndex, blendTreeIndex: blendTreeIndex, speed: speed, loop: loop))
            }
        }

        transitions = []
        if let list = graph["transitions"] as? [[String: Any]] {
            for entry in list {
                let fromState = (entry["from"] as? NSNumber)?.intValue ?? -1
                let toState = (entry["to"] as? NSNumber)?.intValue ?? -1
                let duration = (entry["duration"] as? NSNumber)?.floatValue ?? 0.25
                let hasExitTime = (entry["hasExitTime"] as? NSNumber)?.boolValue ?? false
                let exitTime = (entry["exitTime"] as? NSNumber)?.floatValue ?? 0.9
                let fixedDuration = (entry["fixedDuration"] as? NSNumber)?.boolValue ?? true
                var conditions: [GraphCondition] = []
                if let list = entry["conditions"] as? [[String: Any]] {
                    for cond in list {
                        let parameter = cond["parameter"] as? String ?? ""
                        let op = cond["op"] as? String ?? "IfTrue"
                        let threshold = (cond["threshold"] as? NSNumber)?.floatValue ?? 0
                        let intThreshold = (cond["intThreshold"] as? NSNumber)?.intValue ?? 0
                        let boolThreshold = (cond["boolThreshold"] as? NSNumber)?.boolValue ?? false
                        conditions.append(GraphCondition(parameter: parameter, op: op, threshold: threshold, intThreshold: intThreshold, boolThreshold: boolThreshold))
                    }
                }
                transitions.append(GraphTransition(fromState: fromState, toState: toState, duration: duration, hasExitTime: hasExitTime, exitTime: exitTime, fixedDuration: fixedDuration, conditions: conditions))
            }
        }
    }

    private func applyGraph() {
        let paramPayload = parameters.map { param -> [String: Any] in
            [
                "name": param.name,
                "type": param.type,
                "float": param.floatValue,
                "int": param.intValue,
                "bool": param.boolValue
            ]
        }

        let blendTreePayload = blendTrees.map { tree -> [String: Any] in
            let motions = tree.motions.map { ["clipIndex": $0.clipIndex, "threshold": $0.threshold] }
            return [
                "name": tree.name,
                "type": tree.type,
                "parameter": tree.parameter,
                "motions": motions
            ]
        }

        let statePayload = states.map { state -> [String: Any] in
            [
                "name": state.name,
                "type": state.type,
                "clipIndex": state.clipIndex,
                "blendTree": state.blendTreeIndex,
                "speed": state.speed,
                "loop": state.loop
            ]
        }

        let transitionPayload = transitions.map { transition -> [String: Any] in
            let conditions = transition.conditions.map {
                [
                    "parameter": $0.parameter,
                    "op": $0.op,
                    "threshold": $0.threshold,
                    "intThreshold": $0.intThreshold,
                    "boolThreshold": $0.boolThreshold
                ]
            }
            return [
                "from": transition.fromState,
                "to": transition.toState,
                "duration": transition.duration,
                "hasExitTime": transition.hasExitTime,
                "exitTime": transition.exitTime,
                "fixedDuration": transition.fixedDuration,
                "conditions": conditions
            ]
        }

        let graph: [String: Any] = [
            "parameters": paramPayload,
            "blendTrees": blendTreePayload,
            "states": statePayload,
            "transitions": transitionPayload
        ]

        _ = CrescentEngineBridge.shared().setAnimatorGraph(uuid: entityUUID, graph: graph)
    }

    private func nodePoint(for index: Int, in size: CGSize) -> CGPoint? {
        if index < 0 || index >= states.count {
            return nil
        }
        let id = states[index].id
        return nodePositions[id] ?? defaultPosition(for: states[index], in: size)
    }

    private func defaultPosition(for state: GraphState, in size: CGSize) -> CGPoint {
        guard let idx = states.firstIndex(where: { $0.id == state.id }) else {
            return CGPoint(x: 120, y: 100)
        }
        let columns = max(1, Int(size.width / 220))
        let row = idx / columns
        let column = idx % columns
        let x = CGFloat(120 + column * 220)
        let y = CGFloat(90 + row * 120)
        return CGPoint(x: x, y: min(y, size.height - 80))
    }

    private func initializePositionsIfNeeded(in size: CGSize) {
        if didInitPositions || states.isEmpty {
            return
        }
        for state in states {
            if nodePositions[state.id] == nil {
                nodePositions[state.id] = defaultPosition(for: state, in: size)
            }
        }
        didInitPositions = true
    }
}

struct Vector3InputRow: View {
    let title: String
    @Binding var values: [Float]
    var onChange: (() -> Void)? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title)
                .font(EditorTheme.font(size: 11, weight: .medium))
                .foregroundColor(EditorTheme.textMuted)

            HStack(spacing: 8) {
                TextField("X", value: Binding(
                    get: { values[safe: 0] ?? 0 },
                    set: { newVal in
                        if values.count > 0 { values[0] = newVal }
                        onChange?()
                    }), formatter: NumberFormatter.floatFormatter)
                .textFieldStyle(.roundedBorder)
                .frame(width: 70)
                TextField("Y", value: Binding(
                    get: { values[safe: 1] ?? 0 },
                    set: { newVal in
                        if values.count > 1 { values[1] = newVal }
                        onChange?()
                    }), formatter: NumberFormatter.floatFormatter)
                .textFieldStyle(.roundedBorder)
                .frame(width: 70)
                TextField("Z", value: Binding(
                    get: { values[safe: 2] ?? 0 },
                    set: { newVal in
                        if values.count > 2 { values[2] = newVal }
                        onChange?()
                    }), formatter: NumberFormatter.floatFormatter)
                .textFieldStyle(.roundedBorder)
                .frame(width: 70)
            }
        }
    }
}

private struct GraphNode: View {
    let title: String
    let subtitle: String
    let isBlendTree: Bool
    let position: CGPoint

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title)
                .font(EditorTheme.font(size: 11, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)
            Text(subtitle)
                .font(EditorTheme.font(size: 9))
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(10)
        .frame(width: 160)
        .background(isBlendTree ? Color.blue.opacity(0.25) : Color.teal.opacity(0.25))
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(10)
        .position(position)
    }
}

private struct GraphLine: Shape {
    let from: CGPoint
    let to: CGPoint

    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: from)
        let midX = (from.x + to.x) / 2
        path.addCurve(to: to,
                      control1: CGPoint(x: midX, y: from.y),
                      control2: CGPoint(x: midX, y: to.y))
        return path
    }
}
