import SwiftUI

struct SettingsWindow: View {
    @ObservedObject var editorState: EditorState
    @Environment(\.dismiss) private var dismiss
    
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
            
            ScrollView {
                SettingsPanel(editorState: editorState)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(16)
            }
        }
        .environment(\.colorScheme, .dark)
        .frame(minWidth: 520, minHeight: 620)
        .onAppear {
            if !editorState.settingsWindowRequested {
                dismiss()
            }
        }
    }
}

#Preview {
    SettingsWindow(editorState: EditorState())
}
