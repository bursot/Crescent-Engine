import SwiftUI
import Combine

struct AnimGraphWindow: View {
    @ObservedObject var editorState: EditorState
    @State private var activeUUID: String?
    @State private var clipNames: [String] = []

    private let timer = Timer.publish(every: 0.8, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            LinearGradient(
                colors: [EditorTheme.backgroundTop, EditorTheme.backgroundBottom],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            if let uuid = activeUUID, !uuid.isEmpty {
                AnimatorGraphEditor(entityUUID: uuid, clipNames: clipNames)
            } else {
                EmptyStateView(
                    title: "No Rig Selected",
                    subtitle: "Select an animated object to edit its graph."
                )
            }
        }
        .environment(\.colorScheme, .dark)
        .frame(minWidth: 960, minHeight: 720)
        .onAppear {
            refreshSelection()
        }
        .onChange(of: editorState.selectedEntityUUIDs) { _ in
            refreshSelection()
        }
        .onReceive(timer) { _ in
            refreshSelection()
        }
    }

    private func refreshSelection() {
        let uuid = editorState.selectedEntityUUIDs.first ?? ""
        activeUUID = uuid
        guard !uuid.isEmpty else {
            clipNames = []
            return
        }
        if let info = CrescentEngineBridge.shared().getSkinnedMeshInfo(uuid: uuid) as? [String: Any] {
            if let clips = info["clips"] as? [String] {
                clipNames = clips
            } else if let clips = info["clips"] as? [Any] {
                clipNames = clips.compactMap { $0 as? String }
            }
        }
    }
}

private struct EmptyStateView: View {
    let title: String
    let subtitle: String

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "figure.walk")
                .font(EditorTheme.font(size: 40, weight: .semibold))
                .foregroundColor(EditorTheme.textMuted)
            Text(title)
                .font(EditorTheme.font(size: 14, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)
            Text(subtitle)
                .font(EditorTheme.font(size: 11))
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(20)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}
