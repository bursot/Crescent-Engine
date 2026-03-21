import SwiftUI
import AppKit

struct GameRuntimeView: View {
    @ObservedObject var runtimeState: GameRuntimeState
    @State private var keyboardMonitor: Any?

    var body: some View {
        ZStack(alignment: .bottomLeading) {
            Color.black.ignoresSafeArea()

            MetalView(
                viewKind: .game,
                isActive: runtimeState.isRunning && !runtimeState.isSettingsMenuPresented,
                drivesLoop: true,
                onKeyDownIntercept: { keyCode in
                    guard keyCode == 53 else {
                        return false
                    }
                    runtimeState.toggleSettingsMenu()
                    return true
                },
                onEngineReady: {
                    runtimeState.bootstrapIfNeeded()
                }
            )
            .ignoresSafeArea()

            if runtimeState.isLoading || runtimeState.errorMessage != nil {
                RuntimeOverlay(
                    title: runtimeState.errorMessage == nil ? runtimeState.gameTitle : "Launch Failed",
                    message: runtimeState.errorMessage ?? "Loading game"
                )
            }

            if runtimeState.isRunning && runtimeState.errorMessage == nil && !runtimeState.isLoading && !runtimeState.isSettingsMenuPresented {
                RuntimeHUDHint(text: "ESC  Graphics")
                    .padding(.leading, 26)
                    .padding(.bottom, 24)
                    .transition(.opacity)
            }

            if runtimeState.isSettingsMenuPresented && runtimeState.errorMessage == nil {
                Color.black.opacity(0.34)
                    .ignoresSafeArea()
                    .onTapGesture {
                        runtimeState.closeSettingsMenu()
                    }

                HStack {
                    Spacer(minLength: 0)
                    RuntimeSettingsPanel(runtimeState: runtimeState)
                        .padding(.trailing, 28)
                        .padding(.vertical, 28)
                        .transition(.move(edge: .trailing).combined(with: .opacity))
                }
            }
        }
        .frame(minWidth: 960, minHeight: 540)
        .animation(.spring(response: 0.28, dampingFraction: 0.92), value: runtimeState.isSettingsMenuPresented)
        .onAppear {
            installKeyboardMonitorIfNeeded()
        }
        .onDisappear {
            removeKeyboardMonitor()
        }
    }

    private func installKeyboardMonitorIfNeeded() {
        guard keyboardMonitor == nil else {
            return
        }
        keyboardMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown]) { event in
            if event.keyCode == 53 {
                runtimeState.toggleSettingsMenu()
                return nil
            }
            return event
        }
    }

    private func removeKeyboardMonitor() {
        if let keyboardMonitor {
            NSEvent.removeMonitor(keyboardMonitor)
            self.keyboardMonitor = nil
        }
    }
}

private struct RuntimeOverlay: View {
    let title: String
    let message: String

    var body: some View {
        VStack(spacing: 12) {
            if message == "Loading game" {
                ProgressView()
                    .controlSize(.large)
                    .tint(.white)
            }

            Text(title)
                .font(.system(size: 20, weight: .semibold, design: .rounded))
                .foregroundColor(.white)

            Text(message)
                .font(.system(size: 13, weight: .medium, design: .rounded))
                .foregroundColor(Color.white.opacity(0.76))
                .multilineTextAlignment(.center)
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 20)
        .background(Color.black.opacity(0.68))
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
    }
}

private struct RuntimeHUDHint: View {
    let text: String

    var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold, design: .rounded))
            .foregroundColor(Color.white.opacity(0.76))
            .padding(.horizontal, 14)
            .padding(.vertical, 9)
            .background(Color.black.opacity(0.45))
            .clipShape(Capsule())
            .overlay(
                Capsule()
                    .stroke(Color.white.opacity(0.08), lineWidth: 1)
            )
    }
}

private struct RuntimeSettingsPanel: View {
    @ObservedObject var runtimeState: GameRuntimeState

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Graphics")
                        .font(.system(size: 26, weight: .bold, design: .rounded))
                        .foregroundColor(.white)
                    Text("Runtime rendering and post-process settings")
                        .font(.system(size: 12, weight: .medium, design: .rounded))
                        .foregroundColor(Color.white.opacity(0.62))
                }
                Spacer()
                Button("Reset") {
                    runtimeState.resetGraphicsSettings()
                }
                .buttonStyle(.bordered)
                Button("Close") {
                    runtimeState.closeSettingsMenu()
                }
                .buttonStyle(.borderedProminent)
            }

            ScrollView(showsIndicators: false) {
                VStack(alignment: .leading, spacing: 14) {
                    RuntimeSectionCard(title: "Preset") {
                        VStack(alignment: .leading, spacing: 12) {
                            HStack(spacing: 8) {
                                ForEach(RuntimeGraphicsPreset.allCases) { preset in
                                    Button(preset.rawValue) {
                                        runtimeState.applyGraphicsPreset(preset)
                                    }
                                    .buttonStyle(RuntimePillButtonStyle(isSelected: runtimeState.selectedGraphicsPreset == preset))
                                }
                            }
                            Text("Presetler kalite ve ağır efekt paketini birlikte değiştirir.")
                                .font(.system(size: 11, weight: .medium, design: .rounded))
                                .foregroundColor(Color.white.opacity(0.52))
                        }
                    }

                    RuntimeSectionCard(title: "Quality") {
                        VStack(spacing: 12) {
                            RuntimeSliderRow(
                                title: "Render Scale",
                                valueText: String(format: "%.2fx", runtimeState.graphicsSettings.renderScale),
                                value: binding(\.renderScale),
                                range: 0.5...1.5
                            )
                            RuntimePickerRow(
                                title: "Shadow Quality",
                                selection: binding(\.shadowQuality),
                                options: [0, 1, 2, 3],
                                label: shadowQualityLabel
                            )
                            RuntimePickerRow(
                                title: "Shadow Resolution",
                                selection: binding(\.shadowResolution),
                                options: [512, 1024, 2048, 4096],
                                label: { "\($0)" }
                            )
                            RuntimePickerRow(
                                title: "Texture Quality",
                                selection: binding(\.textureQuality),
                                options: [0, 1, 2, 3],
                                label: textureQualityLabel
                            )
                            RuntimePickerRow(
                                title: "Anisotropy",
                                selection: binding(\.anisotropy),
                                options: [1, 2, 4, 8, 16],
                                label: { "\($0)x" }
                            )
                            RuntimePickerRow(
                                title: "MSAA",
                                selection: binding(\.msaaSamples),
                                options: [1, 2, 4, 8],
                                label: { $0 == 1 ? "Off" : "\($0)x" }
                            )
                            RuntimeEnumPickerRow(
                                title: "Upscaler",
                                selection: binding(\.upscaler),
                                options: RuntimeUpscalerMode.allCases
                            )
                        }
                    }

                    RuntimeSectionCard(title: "Anti-Aliasing") {
                        VStack(spacing: 12) {
                            RuntimeEnumPickerRow(
                                title: "Method",
                                selection: binding(\.antiAliasing),
                                options: RuntimeAntiAliasingMode.allCases
                            )
                            RuntimeToggleRow(title: "Post Process", isOn: binding(\.postProcessingEnabled))
                        }
                    }

                    RuntimeSectionCard(title: "Post FX") {
                        VStack(spacing: 12) {
                            RuntimeToggleSliderRow(
                                title: "Bloom",
                                isOn: binding(\.bloom),
                                sliderValue: binding(\.bloomIntensity),
                                range: 0.0...2.0,
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.bloomIntensity)
                            )
                            RuntimeSliderRow(
                                title: "Bloom Threshold",
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.bloomThreshold),
                                value: binding(\.bloomThreshold),
                                range: 0.0...4.0
                            )
                            .disabled(!runtimeState.graphicsSettings.bloom)

                            RuntimeToggleSliderRow(
                                title: "SSAO",
                                isOn: binding(\.ssao),
                                sliderValue: binding(\.ssaoStrength),
                                range: 0.0...3.0,
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.ssaoStrength)
                            )
                            RuntimeSliderRow(
                                title: "SSAO Radius",
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.ssaoRadius),
                                value: binding(\.ssaoRadius),
                                range: 0.1...3.0
                            )
                            .disabled(!runtimeState.graphicsSettings.ssao)

                            RuntimeToggleSliderRow(
                                title: "SSR",
                                isOn: binding(\.ssr),
                                sliderValue: binding(\.ssrMaxRoughness),
                                range: 0.05...1.0,
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.ssrMaxRoughness)
                            )
                            RuntimeSliderRow(
                                title: "SSR Thickness",
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.ssrThickness),
                                value: binding(\.ssrThickness),
                                range: 0.01...1.0
                            )
                            .disabled(!runtimeState.graphicsSettings.ssr)

                            RuntimeToggleSliderRow(
                                title: "Motion Blur",
                                isOn: binding(\.motionBlur),
                                sliderValue: binding(\.motionBlurStrength),
                                range: 0.0...1.0,
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.motionBlurStrength)
                            )

                            RuntimeToggleSliderRow(
                                title: "Depth Of Field",
                                isOn: binding(\.depthOfField),
                                sliderValue: binding(\.dofFocusDistance),
                                range: 0.1...100.0,
                                valueText: String(format: "%.1fm", runtimeState.graphicsSettings.dofFocusDistance)
                            )
                            RuntimeSliderRow(
                                title: "DOF Aperture",
                                valueText: String(format: "f/%.1f", runtimeState.graphicsSettings.dofAperture),
                                value: binding(\.dofAperture),
                                range: 1.2...16.0
                            )
                            .disabled(!runtimeState.graphicsSettings.depthOfField)

                            RuntimeToggleSliderRow(
                                title: "Vignette",
                                isOn: binding(\.vignette),
                                sliderValue: binding(\.vignetteIntensity),
                                range: 0.0...1.0,
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.vignetteIntensity)
                            )

                            RuntimeToggleSliderRow(
                                title: "Film Grain",
                                isOn: binding(\.filmGrain),
                                sliderValue: binding(\.filmGrainIntensity),
                                range: 0.0...1.0,
                                valueText: String(format: "%.2f", runtimeState.graphicsSettings.filmGrainIntensity)
                            )
                        }
                        .disabled(!runtimeState.graphicsSettings.postProcessingEnabled)
                    }
                }
                .padding(.top, 2)
                .padding(.bottom, 8)
            }
        }
        .padding(22)
        .frame(width: 430)
        .background(
            RoundedRectangle(cornerRadius: 26, style: .continuous)
                .fill(Color(red: 0.08, green: 0.09, blue: 0.11).opacity(0.94))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 26, style: .continuous)
                .stroke(Color.white.opacity(0.08), lineWidth: 1)
        )
        .shadow(color: Color.black.opacity(0.34), radius: 24, x: 0, y: 14)
    }

    private func binding<Value>(_ keyPath: WritableKeyPath<RuntimeGraphicsSettings, Value>) -> Binding<Value> {
        Binding(
            get: { runtimeState.graphicsSettings[keyPath: keyPath] },
            set: { newValue in
                runtimeState.updateGraphicsSettings { settings in
                    settings[keyPath: keyPath] = newValue
                }
            }
        )
    }

    private func shadowQualityLabel(_ value: Int) -> String {
        switch value {
        case 0: return "Low"
        case 1: return "Medium"
        case 2: return "High"
        default: return "Ultra"
        }
    }

    private func textureQualityLabel(_ value: Int) -> String {
        switch value {
        case 0: return "Low"
        case 1: return "Medium"
        case 2: return "High"
        default: return "Ultra"
        }
    }
}

private struct RuntimeSectionCard<Content: View>: View {
    let title: String
    let content: Content

    init(title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(title.uppercased())
                .font(.system(size: 11, weight: .bold, design: .rounded))
                .foregroundColor(Color.white.opacity(0.46))
                .tracking(0.8)

            content
        }
        .padding(16)
        .background(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .fill(Color.white.opacity(0.045))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .stroke(Color.white.opacity(0.05), lineWidth: 1)
        )
    }
}

private struct RuntimeToggleRow: View {
    let title: String
    @Binding var isOn: Bool

    var body: some View {
        Toggle(isOn: $isOn) {
            Text(title)
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .foregroundColor(.white)
        }
        .toggleStyle(.switch)
    }
}

private struct RuntimeSliderRow: View {
    let title: String
    let valueText: String
    @Binding var value: Double
    let range: ClosedRange<Double>

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(title)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundColor(.white)
                Spacer()
                Text(valueText)
                    .font(.system(size: 11, weight: .medium, design: .rounded))
                    .foregroundColor(Color.white.opacity(0.6))
            }

            Slider(value: $value, in: range)
                .tint(.white)
        }
    }
}

private struct RuntimeToggleSliderRow: View {
    let title: String
    @Binding var isOn: Bool
    @Binding var sliderValue: Double
    let range: ClosedRange<Double>
    let valueText: String

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle(isOn: $isOn) {
                HStack {
                    Text(title)
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundColor(.white)
                    Spacer()
                    Text(valueText)
                        .font(.system(size: 11, weight: .medium, design: .rounded))
                        .foregroundColor(Color.white.opacity(0.6))
                }
            }
            .toggleStyle(.switch)

            Slider(value: $sliderValue, in: range)
                .tint(.white)
                .disabled(!isOn)
        }
    }
}

private struct RuntimePickerRow<Value: Hashable>: View {
    let title: String
    @Binding var selection: Value
    let options: [Value]
    let label: (Value) -> String

    var body: some View {
        HStack {
            Text(title)
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .foregroundColor(.white)
            Spacer()
            Picker(title, selection: $selection) {
                ForEach(options, id: \.self) { option in
                    Text(label(option)).tag(option)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .frame(width: 138)
        }
    }
}

private struct RuntimeEnumPickerRow<Value: Hashable & Identifiable & RawRepresentable>: View where Value.RawValue == String {
    let title: String
    @Binding var selection: Value
    let options: [Value]

    var body: some View {
        HStack {
            Text(title)
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .foregroundColor(.white)
            Spacer()
            Picker(title, selection: $selection) {
                ForEach(options) { option in
                    Text(option.rawValue).tag(option)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .frame(width: 138)
        }
    }
}

private struct RuntimePillButtonStyle: ButtonStyle {
    let isSelected: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12, weight: .semibold, design: .rounded))
            .foregroundColor(.white)
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(
                Capsule()
                    .fill(isSelected ? Color.white.opacity(0.18) : Color.white.opacity(configuration.isPressed ? 0.12 : 0.06))
            )
            .overlay(
                Capsule()
                    .stroke(Color.white.opacity(isSelected ? 0.18 : 0.08), lineWidth: 1)
            )
    }
}
