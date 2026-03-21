import Foundation

enum TerrainBrushMode: Int, Equatable {
    case paint = 0
    case sculpt = 1
}

enum TerrainSculptTool: Int, Equatable {
    case raise = 0
    case smooth = 1
    case flatten = 2
}

struct TerrainPaintConfig: Equatable {
    var enabled: Bool = false
    var targetEntityUUID: String = ""
    var mode: TerrainBrushMode = .paint
    var layer: Int = 0
    var sculptTool: TerrainSculptTool = .raise
    var sculptResolution: Int = 128
    var radius: Float = 1.5
    var hardness: Float = 0.65
    var strength: Float = 0.35
    var spacing: Float = 0.25
    var maskPreset: Int = 0
    var maskPath: String = ""
    var autoNormalize: Bool = true
}
