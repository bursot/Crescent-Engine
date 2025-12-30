#include "Common.metal.h"

// ============================================================================
// DEBUG-SPECIFIC STRUCTURES
// ============================================================================

// Debug vertex input
struct DebugVertexIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

// Debug vertex output
struct DebugVertexOut {
    float4 position [[position]];
    float4 color;
    float3 worldPosition;
};

// Debug uniforms
struct DebugUniforms {
    float4x4 viewProjectionMatrix;
    float4 gridColor;
    float4 cameraPosition; // w unused
    float4 gridParams;     // (gridSize, gridFadeStart, gridFadeEnd, gridCellSize)
    float2 gridOrigin;     // snapped origin in XZ
    float2 padding;        // keep 128-byte size (matches C++)
};

// ============================================================================
// DEBUG LINE SHADER
// ============================================================================

vertex DebugVertexOut debugLineVertexShader(
    DebugVertexIn in [[stage_in]],
    constant DebugUniforms& uniforms [[buffer(1)]]
) {
    DebugVertexOut out;
    out.position = uniforms.viewProjectionMatrix * float4(in.position, 1.0);
    out.color = in.color;
    out.worldPosition = in.position;
    return out;
}

fragment float4 debugLineFragmentShader(
    DebugVertexOut in [[stage_in]]
) {
    return in.color;
}

// ============================================================================
// INFINITE GRID SHADER
// ============================================================================

vertex DebugVertexOut gridVertexShader(
    DebugVertexIn in [[stage_in]],
    constant DebugUniforms& uniforms [[buffer(1)]]
) {
    DebugVertexOut out;
    out.position = uniforms.viewProjectionMatrix * float4(in.position, 1.0);
    out.color = in.color;
    out.worldPosition = in.position;
    return out;
}

fragment float4 gridFragmentShader(
    DebugVertexOut in [[stage_in]],
    constant DebugUniforms& uniforms [[buffer(1)]]
) {
    float3 worldPos = in.worldPosition;
    
    // Distance fade
    float dist = length(worldPos - uniforms.cameraPosition.xyz);
    float fade = 1.0 - smoothstep(uniforms.gridParams.y, uniforms.gridParams.z, dist);
    
    if (fade < 0.01) {
        discard_fragment();
    }
    
    // gridParams = (gridSize, fadeStart, fadeEnd, cellSize)
    // cellSize is in .w (last component)
    float cellSize = uniforms.gridParams.w;
    
    // World-space grid coordinates
    float2 coord = worldPos.xz / cellSize;
    
    // Anti-aliased grid lines
    float2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = min(grid.x, grid.y);
    
    // Major grid (every 10 cells)
    float2 majorCoord = coord / 10.0;
    float2 majorGrid = abs(fract(majorCoord - 0.5) - 0.5) / fwidth(majorCoord);
    float majorLine = min(majorGrid.x, majorGrid.y);
    
    // Thin axis lines (2cm thick)
    float axisX = smoothstep(0.05, 0.02, abs(worldPos.z));
    float axisZ = smoothstep(0.05, 0.02, abs(worldPos.x));
    
    // Combine masks
    float gridMask = saturate(1.0 - line) * 0.3 + saturate(1.0 - majorLine) * 0.7;
    float axisMask = max(axisX, axisZ);
    
    // Colors
    float3 gridColor = uniforms.gridColor.rgb;
    float3 axisColor = mix(float3(0.2, 0.8, 0.2), float3(0.8, 0.2, 0.2), step(axisZ, axisX));
    
    float3 finalColor = mix(gridColor, axisColor, axisMask);
    
    // Alpha
    float alpha = (gridMask + axisMask * 0.5) * fade * uniforms.gridColor.a;
    
    if (alpha < 0.01) {
        discard_fragment();
    }
    
    return float4(finalColor, alpha);
}
