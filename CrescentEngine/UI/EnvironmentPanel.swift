import SwiftUI
import UniformTypeIdentifiers
import AppKit
import Combine

final class EnvironmentViewModel: ObservableObject {
    @Published var exposure: Double = 0.0
    @Published var iblIntensity: Double = 1.0
    @Published var skyIntensity: Double = 1.0
    @Published var pitch: Double = 0.0
    @Published var yaw: Double = 0.0
    @Published var roll: Double = 0.0
    @Published var saturation: Double = 1.0
    @Published var contrast: Double = 1.0
    @Published var blur: Double = 0.0
    @Published var tint: Color = .white
    @Published var skyboxVisible: Bool = true
    @Published var currentFileName: String = "Builtin Sky"
    
    weak var editorState: EditorState?
    
    init(editorState: EditorState?) {
        self.editorState = editorState
        refreshFromEngine()
    }
    
    func refreshFromEngine() {
        let dict = CrescentEngineBridge.shared().getEnvironmentSettings() as? [String: Any] ?? [:]
        exposure = dict["exposure"] as? Double ?? 0.0
        iblIntensity = dict["iblIntensity"] as? Double ?? 1.0
        skyIntensity = dict["skyIntensity"] as? Double ?? 1.0
        
        if let rot = dict["rotation"] as? [NSNumber], rot.count >= 3 {
            pitch = rot[0].doubleValue
            yaw = rot[1].doubleValue
            roll = rot[2].doubleValue
        }
        
        if let tintValues = dict["tint"] as? [NSNumber], tintValues.count >= 3 {
            tint = Color(red: tintValues[0].doubleValue,
                         green: tintValues[1].doubleValue,
                         blue: tintValues[2].doubleValue)
        }
        
        saturation = dict["saturation"] as? Double ?? 1.0
        contrast = dict["contrast"] as? Double ?? 1.0
        blur = dict["blur"] as? Double ?? 0.0
        skyboxVisible = (dict["skyboxVisible"] as? NSNumber)?.intValue != 0
        
        if let path = dict["path"] as? String, !path.isEmpty {
            currentFileName = URL(fileURLWithPath: path).lastPathComponent
        }
    }
    
    func loadEnvironment(from url: URL) {
        let hasAccess = url.startAccessingSecurityScopedResource()
        defer {
            if hasAccess { url.stopAccessingSecurityScopedResource() }
        }
        
        if CrescentEngineBridge.shared().loadEnvironmentMap(url.path) {
            currentFileName = url.lastPathComponent
            editorState?.addLog(.info, "Environment loaded: \(url.lastPathComponent)")
            refreshFromEngine()
        } else {
            editorState?.addLog(.error, "Failed to load environment: \(url.lastPathComponent)")
        }
    }
    
    func reset() {
        CrescentEngineBridge.shared().resetEnvironment()
        editorState?.addLog(.info, "Environment reset to builtin sky")
        refreshFromEngine()
    }
    
    func pushExposure() {
        CrescentEngineBridge.shared().setEnvironmentExposure(Float(exposure))
    }
    
    func pushIBL() {
        CrescentEngineBridge.shared().setEnvironmentIBLIntensity(Float(iblIntensity))
    }
    
    func pushSky() {
        CrescentEngineBridge.shared().setEnvironmentSkyIntensity(Float(skyIntensity))
    }
    
    func pushRotation() {
        CrescentEngineBridge.shared().setEnvironmentRotationWithX(Float(pitch),
                                                                  y: Float(yaw),
                                                                  z: Float(roll))
    }
    
    func pushTint() {
        let components = tint.rgbComponents()
        CrescentEngineBridge.shared().setEnvironmentTintWithR(Float(components.r),
                                                              g: Float(components.g),
                                                              b: Float(components.b))
    }
    
    func pushSaturation() {
        CrescentEngineBridge.shared().setEnvironmentSaturation(Float(saturation))
    }
    
    func pushContrast() {
        CrescentEngineBridge.shared().setEnvironmentContrast(Float(contrast))
    }
    
    func pushBlur() {
        CrescentEngineBridge.shared().setEnvironmentBlur(Float(blur))
    }
    
    func pushVisibility() {
        CrescentEngineBridge.shared().setEnvironmentSkyboxVisible(skyboxVisible)
    }
}

struct EnvironmentPanel: View {
    @ObservedObject var viewModel: EnvironmentViewModel
    @State private var showFileImporter = false
    
    private var allowedTypes: [UTType] {
        var types: [UTType] = [.image]
        if let hdr = UTType(filenameExtension: "hdr") { types.append(hdr) }
        if let exr = UTType(filenameExtension: "exr") { types.append(exr) }
        return types
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Environment / IBL")
                        .font(EditorTheme.font(size: 13, weight: .semibold))
                    Text(viewModel.currentFileName)
                        .font(EditorTheme.mono(size: 11))
                        .foregroundColor(EditorTheme.textMuted)
                        .lineLimit(1)
                }
                Spacer()
                Button(action: viewModel.reset) {
                    Label("Reset", systemImage: "arrow.counterclockwise")
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
            }
            
            Button {
                showFileImporter = true
            } label: {
                Label("Load HDR / EXR", systemImage: "folder.fill.badge.plus")
                    .font(EditorTheme.font(size: 12, weight: .semibold))
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(.accentColor)
            
            Toggle("Show Skybox", isOn: $viewModel.skyboxVisible)
                .toggleStyle(.switch)
                .onChange(of: viewModel.skyboxVisible) { _ in viewModel.pushVisibility() }
            
            Divider()
                .overlay(EditorTheme.panelStroke)
            
            EnvironmentSlider(title: "Exposure (EV)", value: $viewModel.exposure, range: -8...8, onCommit: viewModel.pushExposure)
            EnvironmentSlider(title: "IBL Intensity", value: $viewModel.iblIntensity, range: 0...4, onCommit: viewModel.pushIBL)
            EnvironmentSlider(title: "Sky Brightness", value: $viewModel.skyIntensity, range: 0...4, onCommit: viewModel.pushSky)
            EnvironmentSlider(title: "Background Blur (LOD)", value: $viewModel.blur, range: 0...8, onCommit: viewModel.pushBlur)
            EnvironmentSlider(title: "Saturation", value: $viewModel.saturation, range: 0...3, onCommit: viewModel.pushSaturation)
            EnvironmentSlider(title: "Contrast", value: $viewModel.contrast, range: 0.2...3, onCommit: viewModel.pushContrast)
            
            VStack(alignment: .leading, spacing: 6) {
                Text("Rotation (°)")
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(.secondary)
                EnvironmentSlider(title: "Yaw", value: $viewModel.yaw, range: -180...180, onCommit: viewModel.pushRotation)
                EnvironmentSlider(title: "Pitch", value: $viewModel.pitch, range: -90...90, onCommit: viewModel.pushRotation)
                EnvironmentSlider(title: "Roll", value: $viewModel.roll, range: -180...180, onCommit: viewModel.pushRotation)
            }
            
            ColorPicker("Tint", selection: $viewModel.tint, supportsOpacity: false)
                .onChange(of: viewModel.tint) { _ in viewModel.pushTint() }
                .font(EditorTheme.font(size: 11, weight: .semibold))
        }
        .padding(14)
        .frame(width: 380)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .onAppear {
            viewModel.refreshFromEngine()
        }
        .fileImporter(isPresented: $showFileImporter, allowedContentTypes: allowedTypes, allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                if let first = urls.first {
                    viewModel.loadEnvironment(from: first)
                }
            case .failure(let error):
                viewModel.editorState?.addLog(.error, "Environment import failed: \(error.localizedDescription)")
            }
        }
    }
}

struct EnvironmentSlider: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var onCommit: () -> Void
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                    .font(EditorTheme.font(size: 11, weight: .semibold))
                    .foregroundColor(EditorTheme.textMuted)
                Spacer()
                Text(String(format: "%.2f", value))
                    .font(EditorTheme.mono(size: 11))
                    .foregroundColor(.secondary)
            }
            Slider(value: $value, in: range)
                .onChange(of: value) { _ in onCommit() }
        }
    }
}

private extension Color {
    func rgbComponents() -> (r: Double, g: Double, b: Double) {
        #if os(macOS)
        let ns = NSColor(self)
        return (Double(ns.redComponent), Double(ns.greenComponent), Double(ns.blueComponent))
        #else
        return (1.0, 1.0, 1.0)
        #endif
    }
}
