import SwiftUI

enum EditorTheme {
    static var isDark: Bool = true

    static let shellCornerRadius: CGFloat = 24
    static let panelCornerRadius: CGFloat = 18
    static let sectionCornerRadius: CGFloat = 14
    static let controlCornerRadius: CGFloat = 11
    
    static var backgroundTop: Color {
        color(
            dark: Color(red: 0.03, green: 0.05, blue: 0.09),
            light: Color(red: 0.92, green: 0.94, blue: 0.98)
        )
    }
    
    static var backgroundBottom: Color {
        color(
            dark: Color(red: 0.01, green: 0.02, blue: 0.05),
            light: Color(red: 0.86, green: 0.89, blue: 0.95)
        )
    }

    static var shellTop: Color {
        color(
            dark: Color(red: 0.08, green: 0.11, blue: 0.18),
            light: Color(red: 0.95, green: 0.96, blue: 0.99)
        )
    }

    static var shellBottom: Color {
        color(
            dark: Color(red: 0.04, green: 0.06, blue: 0.12),
            light: Color(red: 0.88, green: 0.91, blue: 0.96)
        )
    }
    
    static var panelBackground: Color {
        color(
            dark: Color(red: 0.10, green: 0.13, blue: 0.20),
            light: Color(red: 0.97, green: 0.98, blue: 0.99)
        )
    }
    
    static var panelHeader: Color {
        color(
            dark: Color(red: 0.13, green: 0.17, blue: 0.25),
            light: Color(red: 0.93, green: 0.95, blue: 0.98)
        )
    }
    
    static var panelStroke: Color {
        color(
            dark: Color.white.opacity(0.09),
            light: Color.black.opacity(0.08)
        )
    }

    static var panelStrokeStrong: Color {
        color(
            dark: Color(red: 0.46, green: 0.61, blue: 0.78).opacity(0.28),
            light: Color.black.opacity(0.12)
        )
    }
    
    static var surface: Color {
        color(
            dark: Color(red: 0.15, green: 0.19, blue: 0.28),
            light: Color(red: 0.90, green: 0.92, blue: 0.96)
        )
    }
    
    static var surfaceElevated: Color {
        color(
            dark: Color(red: 0.20, green: 0.26, blue: 0.38),
            light: Color(red: 0.95, green: 0.96, blue: 0.99)
        )
    }

    static var surfaceMuted: Color {
        color(
            dark: Color(red: 0.12, green: 0.15, blue: 0.23),
            light: Color(red: 0.93, green: 0.95, blue: 0.98)
        )
    }
    
    static var textPrimary: Color {
        color(dark: Color.white.opacity(0.92), light: Color.black.opacity(0.85))
    }
    
    static var textMuted: Color {
        color(dark: Color.white.opacity(0.68), light: Color.black.opacity(0.55))
    }
    
    static var textAccent: Color {
        color(dark: Color(red: 0.37, green: 0.80, blue: 0.98), light: Color(red: 0.14, green: 0.39, blue: 0.66))
    }

    static var accentSecondary: Color {
        color(dark: Color(red: 0.98, green: 0.74, blue: 0.42), light: Color(red: 0.77, green: 0.47, blue: 0.14))
    }

    static var success: Color {
        color(dark: Color(red: 0.32, green: 0.86, blue: 0.55), light: Color(red: 0.12, green: 0.55, blue: 0.28))
    }

    static var warning: Color {
        color(dark: Color(red: 1.00, green: 0.76, blue: 0.33), light: Color(red: 0.76, green: 0.51, blue: 0.10))
    }

    static var danger: Color {
        color(dark: Color(red: 1.00, green: 0.40, blue: 0.42), light: Color(red: 0.74, green: 0.18, blue: 0.22))
    }
    
    static let fontTitle = Font.custom("Avenir Next Demi Bold", size: 12)
    static let fontHero = Font.custom("Avenir Next Heavy", size: 18)
    static let fontBody = Font.custom("Avenir Next", size: 11)
    static let fontBodyMedium = Font.custom("Avenir Next Medium", size: 11)
    static let fontCaption = Font.custom("Avenir Next", size: 10)
    static let fontMono = Font.custom("Menlo", size: 11)
    
    static func font(size: CGFloat, weight: Font.Weight = .regular) -> Font {
        let name: String
        switch weight {
        case .semibold, .bold:
            name = "Avenir Next Demi Bold"
        case .medium:
            name = "Avenir Next Medium"
        default:
            name = "Avenir Next"
        }
        return Font.custom(name, size: size)
    }
    
    static func mono(size: CGFloat) -> Font {
        Font.custom("Menlo", size: size)
    }

    static var appBackdrop: some View {
        ZStack {
            LinearGradient(
                colors: [backgroundTop, backgroundBottom],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )

            RadialGradient(
                colors: [textAccent.opacity(0.24), Color.clear],
                center: .topTrailing,
                startRadius: 40,
                endRadius: 540
            )
            .blendMode(.screen)

            RadialGradient(
                colors: [accentSecondary.opacity(0.18), Color.clear],
                center: .bottomLeading,
                startRadius: 40,
                endRadius: 480
            )
            .blendMode(.screen)
        }
    }

    static var shellGradient: LinearGradient {
        LinearGradient(
            colors: [shellTop, shellBottom],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
    }

    static var panelGradient: LinearGradient {
        LinearGradient(
            colors: [panelHeader.opacity(0.98), panelBackground.opacity(0.98)],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
    }

    static var sectionGradient: LinearGradient {
        LinearGradient(
            colors: [surfaceElevated.opacity(0.86), surface.opacity(0.96)],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
    }

    static var inputGradient: LinearGradient {
        LinearGradient(
            colors: [surface.opacity(0.98), surfaceMuted.opacity(0.96)],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
    }

    static var viewportGradient: LinearGradient {
        LinearGradient(
            colors: [
                Color(red: 0.14, green: 0.19, blue: 0.31),
                Color(red: 0.08, green: 0.11, blue: 0.18)
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
    }

    static var panelShadow: Color {
        Color.black.opacity(isDark ? 0.28 : 0.12)
    }
    
    private static func color(dark: Color, light: Color) -> Color {
        isDark ? dark : light
    }
}

extension View {
    func editorPanel(cornerRadius: CGFloat = EditorTheme.panelCornerRadius) -> some View {
        self
            .background(EditorTheme.panelGradient)
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .stroke(EditorTheme.panelStrokeStrong, lineWidth: 1)
            )
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .inset(by: 1)
                    .stroke(EditorTheme.panelStroke.opacity(0.65), lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
            .shadow(color: EditorTheme.panelShadow, radius: 16, x: 0, y: 8)
    }

    func editorSection(cornerRadius: CGFloat = EditorTheme.sectionCornerRadius) -> some View {
        self
            .background(EditorTheme.sectionGradient)
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
    }

    func editorInput(cornerRadius: CGFloat = EditorTheme.controlCornerRadius) -> some View {
        self
            .background(EditorTheme.inputGradient)
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
    }

    func editorShell() -> some View {
        self
            .background(EditorTheme.shellGradient)
            .overlay(
                RoundedRectangle(cornerRadius: EditorTheme.shellCornerRadius, style: .continuous)
                    .stroke(EditorTheme.panelStrokeStrong, lineWidth: 1)
            )
            .overlay(
                RoundedRectangle(cornerRadius: EditorTheme.shellCornerRadius, style: .continuous)
                    .inset(by: 1)
                    .stroke(EditorTheme.panelStroke.opacity(0.45), lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: EditorTheme.shellCornerRadius, style: .continuous))
            .shadow(color: Color.black.opacity(0.44), radius: 26, x: 0, y: 20)
    }
}
