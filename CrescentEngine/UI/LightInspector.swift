import SwiftUI
import Combine

struct LightInspector: View {
    let uuid: String
    
    @State private var lightData: [String: Any] = [:]
    @State private var color: [Float] = [1,1,1]
    @State private var temperature: Float = 6500
    @State private var intensity: Float = 10
    @State private var unitIndex: Int = 0
    @State private var range: Float = 10
    @State private var falloff: Int = 1
    @State private var castsShadows: Bool = true
    @State private var softShadows: Bool = true
    @State private var shadowRes: Int = 512
    @State private var bias: Float = 0.0005
    @State private var normalBias: Float = 0.001
    @State private var penumbra: Float = 1.0
    @State private var shadowNear: Float = 0.1
    @State private var shadowFar: Float = 200.0
    @State private var contactShadows: Bool = false
    @State private var cascadeCount: Int = 4
    @State private var cascadeSplits: [Float] = [0.05, 0.15, 0.35, 1.0]
    @State private var volumetric: Bool = false
    
    private let resOptions = [256, 512, 1024, 2048]
    private let unitOptions = ["Lumens", "Lux", "Nits"]
    private let falloffOptions = ["Linear", "Inverse Square"]
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("Light")
                    .font(EditorTheme.font(size: 12, weight: .semibold))
                Spacer()
            }
            ColorPicker("Color", selection: Binding(
                get: { Color(.sRGB, red: Double(color[0]), green: Double(color[1]), blue: Double(color[2]), opacity: 1) },
                set: { newCol in
                    let comps = newCol.cgColor?.components ?? [1,1,1,1]
                    color = [Float(comps[0]), Float(comps[1]), Float(comps[2])]
                    push(["color": color])
                }), supportsOpacity: false)
            SliderWithValue(label: "Temperature (K)", value: $temperature, range: 1000...20000, step: 100) { push(["temperatureK": $0]) }
            SliderWithValue(label: "Intensity", value: $intensity, range: 0...100, step: 0.1) { push(["intensity": $0]) }
            Picker("Unit", selection: Binding(get: { unitIndex }, set: { unitIndex = $0; push(["intensityUnit": $0]) })) {
                ForEach(0..<unitOptions.count, id: \.self) { idx in
                    Text(unitOptions[idx]).tag(idx)
                }
            }.pickerStyle(.segmented)
            SliderWithValue(label: "Range", value: $range, range: 0.1...100, step: 0.5) { push(["range": $0]) }
            Picker("Falloff", selection: Binding(get: { falloff }, set: { falloff = $0; push(["falloff": $0]) })) {
                ForEach(0..<falloffOptions.count, id: \.self) { idx in
                    Text(falloffOptions[idx]).tag(idx)
                }
            }.pickerStyle(.segmented)
            Toggle("Cast Shadows", isOn: Binding(get: { castsShadows }, set: { castsShadows = $0; push(["castsShadows": $0]) }))
            if castsShadows {
                Picker("Shadow Resolution", selection: Binding(get: { shadowRes }, set: { shadowRes = $0; push(["shadowResolution": $0]) })) {
                    ForEach(resOptions, id: \.self) { r in Text("\(r)") }
                }
                SliderWithValue(label: "Bias", value: $bias, range: 0.0...0.005, step: 0.00001) { push(["shadowBias": $0]) }
                SliderWithValue(label: "Normal Bias", value: $normalBias, range: 0.0...0.01, step: 0.0001) { push(["shadowNormalBias": $0]) }
                Toggle("Soft Shadows", isOn: Binding(get: { softShadows }, set: { softShadows = $0; push(["softShadows": $0]) }))
                Toggle("Contact Shadows", isOn: Binding(get: { contactShadows }, set: { contactShadows = $0; push(["contactShadows": $0]) }))
                SliderWithValue(label: "Penumbra", value: $penumbra, range: 0...5, step: 0.1) { push(["penumbra": $0]) }
                if let type = lightData["type"] as? Int, type == 0 {
                    SliderWithValue(label: "Shadow Distance", value: $shadowFar, range: 10...1000, step: 1) {
                        push(["shadowNearPlane": shadowNear, "shadowFarPlane": $0])
                    }
                    Stepper("Cascades: \(cascadeCount)", value: $cascadeCount, in: 1...4, onEditingChanged: { _ in push(["cascadeCount": cascadeCount]) })
                    VStack(alignment: .leading) {
                        Text("Cascade Splits")
                        .font(EditorTheme.font(size: 11, weight: .medium))
                        ForEach(0..<4) { idx in
                            SliderWithValue(label: "Split \(idx+1)", value: Binding(
                                get: { cascadeSplits[idx] },
                                set: { cascadeSplits[idx] = $0; push(["cascadeSplits": cascadeSplits]) }
                            ), range: 0...1, step: 0.01) { _ in }
                        }
                    }
                }
            }
            Toggle("Volumetric", isOn: Binding(get: { volumetric }, set: { volumetric = $0; push(["volumetric": $0]) }))
            
            Divider()
            DebugToggles()
        }
        .onAppear { refresh() }
        .onReceive(Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()) { _ in refresh() }
    }
    
    func refresh() {
        let bridge = CrescentEngineBridge.shared()
        guard let info = bridge.getLightInfo(uuid) as? [String: Any], !info.isEmpty else { return }
        lightData = info
        if let col = info["color"] as? [NSNumber], col.count == 3 { color = col.map { $0.floatValue } }
        temperature = (info["temperatureK"] as? NSNumber)?.floatValue ?? temperature
        intensity = (info["intensity"] as? NSNumber)?.floatValue ?? intensity
        unitIndex = (info["intensityUnit"] as? NSNumber)?.intValue ?? unitIndex
        range = (info["range"] as? NSNumber)?.floatValue ?? range
        falloff = (info["falloff"] as? NSNumber)?.intValue ?? falloff
        castsShadows = (info["castsShadows"] as? NSNumber)?.boolValue ?? castsShadows
        shadowRes = (info["shadowResolution"] as? NSNumber)?.intValue ?? shadowRes
        bias = (info["shadowBias"] as? NSNumber)?.floatValue ?? bias
        normalBias = (info["shadowNormalBias"] as? NSNumber)?.floatValue ?? normalBias
        shadowNear = (info["shadowNearPlane"] as? NSNumber)?.floatValue ?? shadowNear
        shadowFar = (info["shadowFarPlane"] as? NSNumber)?.floatValue ?? shadowFar
        softShadows = (info["softShadows"] as? NSNumber)?.boolValue ?? softShadows
        contactShadows = (info["contactShadows"] as? NSNumber)?.boolValue ?? contactShadows
        penumbra = (info["penumbra"] as? NSNumber)?.floatValue ?? penumbra
        cascadeCount = (info["cascadeCount"] as? NSNumber)?.intValue ?? cascadeCount
        if let splits = info["cascadeSplits"] as? [NSNumber], splits.count == 4 {
            cascadeSplits = splits.map { $0.floatValue }
        }
        volumetric = (info["volumetric"] as? NSNumber)?.boolValue ?? volumetric
    }
    
    func push(_ dict: [String: Any]) {
        var payload = dict
        payload["uuid"] = uuid
        _ = CrescentEngineBridge.shared().setLightInfo(uuid, info: payload)
    }
}

struct DebugToggles: View {
    @State private var showAtlas = false
    @State private var showCascades = false
    @State private var showPointFrusta = false
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Debug")
                .font(EditorTheme.font(size: 12, weight: .semibold))
            Toggle("Show Shadow Atlas", isOn: Binding(get: { showAtlas }, set: {
                showAtlas = $0
                CrescentEngineBridge.shared().setDebugDrawShadowAtlas($0)
            }))
            Toggle("Show Cascades", isOn: Binding(get: { showCascades }, set: {
                showCascades = $0
                CrescentEngineBridge.shared().setDebugDrawCascades($0)
            }))
            Toggle("Show Point Frusta", isOn: Binding(get: { showPointFrusta }, set: {
                showPointFrusta = $0
                CrescentEngineBridge.shared().setDebugDrawPointFrusta($0)
            }))
        }
    }
}

struct SliderWithValue: View {
    let label: String
    @Binding var value: Float
    let range: ClosedRange<Float>
    var step: Float? = nil
    var onCommit: ((Float) -> Void)? = nil

    var body: some View {
        VStack(alignment: .leading) {
            HStack {
                Text(label)
                    .font(EditorTheme.font(size: 11, weight: .medium))
                Spacer()
                // Better formatting based on value magnitude
                Text(formattedValue)
                    .font(EditorTheme.mono(size: 11))
                    .foregroundColor(.secondary)
            }
            if let stepValue = step {
                Slider(value: Binding(get: { Double(value) }, set: {
                    value = Float($0)
                    onCommit?(value)
                }), in: Double(range.lowerBound)...Double(range.upperBound), step: Double(stepValue))
            } else {
                Slider(value: Binding(get: { Double(value) }, set: {
                    value = Float($0)
                    onCommit?(value)
                }), in: Double(range.lowerBound)...Double(range.upperBound))
            }
        }
    }

    private var formattedValue: String {
        if value < 0.001 {
            return String(format: "%.6f", value)
        } else if value < 1 {
            return String(format: "%.4f", value)
        } else if value < 100 {
            return String(format: "%.2f", value)
        } else {
            return String(format: "%.0f", value)
        }
    }
}
