import Foundation

struct TerrainPaintConfig: Equatable {
    var enabled: Bool = false
    var targetEntityUUID: String = ""
    var layer: Int = 0
    var radius: Float = 1.5
    var hardness: Float = 0.65
    var strength: Float = 0.35
    var spacing: Float = 0.25
    var maskPreset: Int = 0
    var maskPath: String = ""
    var autoNormalize: Bool = true
}
