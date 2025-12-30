import SwiftUI

enum EditorTheme {
    static var isDark: Bool = true
    
    static var backgroundTop: Color {
        color(
            dark: Color(red: 0.06, green: 0.08, blue: 0.14),
            light: Color(red: 0.92, green: 0.94, blue: 0.98)
        )
    }
    
    static var backgroundBottom: Color {
        color(
            dark: Color(red: 0.03, green: 0.04, blue: 0.08),
            light: Color(red: 0.86, green: 0.89, blue: 0.95)
        )
    }
    
    static var panelBackground: Color {
        color(
            dark: Color(red: 0.11, green: 0.13, blue: 0.18),
            light: Color(red: 0.97, green: 0.98, blue: 0.99)
        )
    }
    
    static var panelHeader: Color {
        color(
            dark: Color(red: 0.14, green: 0.16, blue: 0.22),
            light: Color(red: 0.93, green: 0.95, blue: 0.98)
        )
    }
    
    static var panelStroke: Color {
        color(
            dark: Color.white.opacity(0.08),
            light: Color.black.opacity(0.08)
        )
    }
    
    static var surface: Color {
        color(
            dark: Color(red: 0.17, green: 0.20, blue: 0.28),
            light: Color(red: 0.90, green: 0.92, blue: 0.96)
        )
    }
    
    static var surfaceElevated: Color {
        color(
            dark: Color(red: 0.20, green: 0.24, blue: 0.33),
            light: Color(red: 0.95, green: 0.96, blue: 0.99)
        )
    }
    
    static var textPrimary: Color {
        color(dark: Color.white.opacity(0.92), light: Color.black.opacity(0.85))
    }
    
    static var textMuted: Color {
        color(dark: Color.white.opacity(0.68), light: Color.black.opacity(0.55))
    }
    
    static var textAccent: Color {
        color(dark: Color(red: 0.42, green: 0.84, blue: 0.94), light: Color(red: 0.14, green: 0.39, blue: 0.66))
    }
    
    static let fontTitle = Font.custom("Avenir Next Demi Bold", size: 12)
    static let fontHero = Font.custom("Avenir Next Demi Bold", size: 18)
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
    
    private static func color(dark: Color, light: Color) -> Color {
        isDark ? dark : light
    }
}
