#include "Common.metal.h"

using namespace metal;

struct ShadowVertexIn {
    float3 position [[attribute(0)]];
};

struct ShadowVertexInSkinned {
    float3 position [[attribute(0)]];
    uint4 boneIndices [[attribute(6)]];
    float4 boneWeights [[attribute(7)]];
};

inline float4 applySkinning(ShadowVertexInSkinned in, const device float4x4* bones) {
    float4 localPos = float4(in.position, 1.0);
    float totalWeight = in.boneWeights.x + in.boneWeights.y + in.boneWeights.z + in.boneWeights.w;
    if (totalWeight <= 0.0) {
        return localPos;
    }
    float4 weights = in.boneWeights / totalWeight;
    float4x4 skin = bones[in.boneIndices.x] * weights.x +
                    bones[in.boneIndices.y] * weights.y +
                    bones[in.boneIndices.z] * weights.z +
                    bones[in.boneIndices.w] * weights.w;
    return skin * localPos;
}

// Depth-only vertex for directional cascades and projected shadows.
vertex float4 shadow_dir_vertex(ShadowVertexIn in [[stage_in]],
                                constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex float4 shadow_dir_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                        constant float4x4& mvp [[buffer(1)]],
                                        const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}

vertex float4 shadow_spot_vertex(ShadowVertexIn in [[stage_in]],
                                 constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex float4 shadow_spot_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                         constant float4x4& mvp [[buffer(1)]],
                                         const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}

// For point lights - uses perspective depth, shader converts for comparison
vertex float4 shadow_point_vertex(ShadowVertexIn in [[stage_in]],
                                  constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex float4 shadow_point_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                          constant float4x4& mvp [[buffer(1)]],
                                          const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}

// Area light approximated with projected shadow map.
vertex float4 shadow_area_vertex(ShadowVertexIn in [[stage_in]],
                                 constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex float4 shadow_area_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                         constant float4x4& mvp [[buffer(1)]],
                                         const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}
