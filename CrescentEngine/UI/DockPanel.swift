import SwiftUI

struct DockPanel: View {
    @ObservedObject var editorState: EditorState
    
    private var availableTabs: [DockTab] {
        var tabs: [DockTab] = []
        if editorState.showAssets { tabs.append(.assets) }
        if editorState.showConsole { tabs.append(.console) }
        return tabs
    }
    
    var body: some View {
        VStack(spacing: 8) {
            HStack(spacing: 8) {
                ForEach(availableTabs) { tab in
                    DockTabButton(
                        title: tab.title,
                        systemImage: tab.icon,
                        isSelected: tab == editorState.dockTab
                    ) {
                        editorState.dockTab = tab
                    }
                }
                
                Spacer()
                
                if editorState.dockTab == .assets {
                    if editorState.hasProject {
                        HStack(spacing: 6) {
                            Image(systemName: "folder.fill")
                                .font(EditorTheme.fontCaption)
                            Text(editorState.projectName.isEmpty ? "Project" : editorState.projectName)
                                .font(EditorTheme.fontCaption)
                                .lineLimit(1)
                        }
                        .foregroundColor(EditorTheme.textMuted)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 6)
                        .background(EditorTheme.surface)
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(EditorTheme.panelStroke, lineWidth: 1)
                        )
                        .cornerRadius(8)
                    } else {
                        Button(action: {
                            editorState.pickAssetRoot()
                        }) {
                            HStack(spacing: 6) {
                                Image(systemName: "folder.fill")
                                    .font(EditorTheme.fontCaption)
                                Text(editorState.assetRootName)
                                    .font(EditorTheme.fontCaption)
                                    .lineLimit(1)
                            }
                            .foregroundColor(EditorTheme.textMuted)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 6)
                            .background(EditorTheme.surface)
                            .overlay(
                                RoundedRectangle(cornerRadius: 8)
                                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
                            )
                            .cornerRadius(8)
                        }
                        .buttonStyle(.plain)
                        .help("Select Asset Root")
                    }
                } else if editorState.dockTab == .console {
                    HStack(spacing: 6) {
                        Image(systemName: "waveform.path.ecg")
                            .font(EditorTheme.fontCaption)
                            .foregroundColor(EditorTheme.textMuted)
                        Text("\(editorState.consoleLogs.count) logs")
                            .font(EditorTheme.fontCaption)
                            .foregroundColor(EditorTheme.textMuted)
                    }
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(EditorTheme.panelHeader)
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
                    .allowsHitTesting(false)
            )
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .zIndex(1)
            
            Group {
                if editorState.showAssets && editorState.showConsole {
                    ZStack {
                        AssetBrowserPanel(editorState: editorState)
                            .opacity(editorState.dockTab == .assets ? 1 : 0)
                            .allowsHitTesting(editorState.dockTab == .assets)
                        ConsolePanel(editorState: editorState)
                            .opacity(editorState.dockTab == .console ? 1 : 0)
                            .allowsHitTesting(editorState.dockTab == .console)
                    }
                } else if editorState.showAssets {
                    AssetBrowserPanel(editorState: editorState)
                } else if editorState.showConsole {
                    ConsolePanel(editorState: editorState)
                }
            }
            .transition(.opacity)
            .frame(maxHeight: .infinity)
            .zIndex(0)
        }
        .clipped()
    }
}

private struct DockTabButton: View {
    let title: String
    let systemImage: String
    let isSelected: Bool
    let action: () -> Void
    
    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: systemImage)
                .font(EditorTheme.fontBody)
            Text(title)
                .font(EditorTheme.fontBodyMedium)
        }
        .foregroundColor(isSelected ? EditorTheme.textPrimary : EditorTheme.textMuted)
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(isSelected ? EditorTheme.surfaceElevated : EditorTheme.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(isSelected ? EditorTheme.textAccent.opacity(0.6) : EditorTheme.panelStroke, lineWidth: 1)
        )
        .cornerRadius(8)
        .contentShape(Rectangle())
        .onTapGesture {
            action()
        }
    }
}
