#include "Common.metal.h"

using namespace metal;

struct ShadowVertexIn {
    float3 position [[attribute(0)]];
};

struct ShadowVertexInUV {
    float3 position [[attribute(0)]];
    float2 texCoord [[attribute(2)]];
};

struct ShadowVertexInSkinned {
    float3 position [[attribute(0)]];
    uint4 boneIndices [[attribute(6)]];
    float4 boneWeights [[attribute(7)]];
};

struct ShadowVertexInSkinnedUV {
    float3 position [[attribute(0)]];
    float2 texCoord [[attribute(2)]];
    uint4 boneIndices [[attribute(6)]];
    float4 boneWeights [[attribute(7)]];
};

struct ShadowOut {
    float4 position [[position]];
    float2 uv;
};

struct ShadowMaterial {
    float4 albedo;
    float4 uvTilingOffset;
    float4 alphaParams; // x cutoff, y hasAlbedoTex, z alphaClip, w unused
};

struct ShadowFoliageParams {
    float4 foliageParams0;
    float4 foliageParams1;
    float4 foliageParams2;
    float4 foliageParams3;
    float4 cameraTime;   // xyz camera position, w time
    float4 boundsCenter; // xyz center
    float4 boundsSize;   // xyz size
    float4 flags;        // x billboard
};

inline float3 applyWindOffsetShadow(float3 worldPos,
                                    constant ShadowFoliageParams& params) {
    if (params.foliageParams2.x < 0.5 || params.foliageParams0.x <= 0.0001) {
        return worldPos;
    }
    float3 dir = params.foliageParams3.xyz;
    if (dot(dir, dir) < 0.0001) {
        dir = float3(1.0, 0.0, 0.0);
    }
    dir = normalize(dir);
    float time = params.cameraTime.w;
    float phase = dot(worldPos.xz, dir.xz) * params.foliageParams0.z + time * params.foliageParams0.y;
    float sway = sin(phase) * params.foliageParams0.x;
    float gust = sin(phase * 0.7 + time * params.foliageParams0.y * 0.5) * params.foliageParams0.w;
    return worldPos + dir * (sway + gust);
}

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

inline float4 applySkinning(ShadowVertexInSkinnedUV in, const device float4x4* bones) {
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

fragment void shadow_alpha_fragment(ShadowOut in [[stage_in]],
                                    constant ShadowMaterial& material [[buffer(0)]],
                                    texture2d<float> albedoMap [[texture(0)]],
                                    sampler alphaSampler [[sampler(0)]]) {
    if (material.alphaParams.z < 0.5) {
        return;
    }
    float2 uv = in.uv * material.uvTilingOffset.xy + material.uvTilingOffset.zw;
    float alpha = material.albedo.a;
    if (material.alphaParams.y > 0.5) {
        alpha *= albedoMap.sample(alphaSampler, uv).a;
    }
    if (alpha < material.alphaParams.x) {
        discard_fragment();
    }
}

// Depth-only vertex for directional cascades and projected shadows.
vertex float4 shadow_dir_vertex(ShadowVertexIn in [[stage_in]],
                                constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex ShadowOut shadow_dir_vertex_cutout(ShadowVertexInUV in [[stage_in]],
                                          constant float4x4& mvp [[buffer(1)]]) {
    ShadowOut out;
    out.position = mvp * float4(in.position, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_dir_vertex_instanced(ShadowVertexIn in [[stage_in]],
                                          constant float4x4& viewProj [[buffer(1)]],
                                          const device InstanceData* instances [[buffer(2)]],
                                          constant ShadowFoliageParams& foliage [[buffer(3)]],
                                          uint instanceId [[instance_id]]) {
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    return viewProj * float4(worldPos, 1.0);
}

vertex ShadowOut shadow_dir_vertex_cutout_instanced(ShadowVertexInUV in [[stage_in]],
                                                    constant float4x4& viewProj [[buffer(1)]],
                                                    const device InstanceData* instances [[buffer(2)]],
                                                    constant ShadowFoliageParams& foliage [[buffer(3)]],
                                                    uint instanceId [[instance_id]]) {
    ShadowOut out;
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    out.position = viewProj * float4(worldPos, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_dir_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                        constant float4x4& mvp [[buffer(1)]],
                                        const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}

vertex ShadowOut shadow_dir_vertex_cutout_skinned(ShadowVertexInSkinnedUV in [[stage_in]],
                                                  constant float4x4& mvp [[buffer(1)]],
                                                  const device float4x4* bones [[buffer(2)]]) {
    ShadowOut out;
    out.position = mvp * applySkinning(in, bones);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_spot_vertex(ShadowVertexIn in [[stage_in]],
                                 constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex ShadowOut shadow_spot_vertex_cutout(ShadowVertexInUV in [[stage_in]],
                                           constant float4x4& mvp [[buffer(1)]]) {
    ShadowOut out;
    out.position = mvp * float4(in.position, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_spot_vertex_instanced(ShadowVertexIn in [[stage_in]],
                                           constant float4x4& viewProj [[buffer(1)]],
                                           const device InstanceData* instances [[buffer(2)]],
                                           constant ShadowFoliageParams& foliage [[buffer(3)]],
                                           uint instanceId [[instance_id]]) {
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    return viewProj * float4(worldPos, 1.0);
}

vertex ShadowOut shadow_spot_vertex_cutout_instanced(ShadowVertexInUV in [[stage_in]],
                                                     constant float4x4& viewProj [[buffer(1)]],
                                                     const device InstanceData* instances [[buffer(2)]],
                                                     constant ShadowFoliageParams& foliage [[buffer(3)]],
                                                     uint instanceId [[instance_id]]) {
    ShadowOut out;
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    out.position = viewProj * float4(worldPos, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_spot_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                         constant float4x4& mvp [[buffer(1)]],
                                         const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}

vertex ShadowOut shadow_spot_vertex_cutout_skinned(ShadowVertexInSkinnedUV in [[stage_in]],
                                                   constant float4x4& mvp [[buffer(1)]],
                                                   const device float4x4* bones [[buffer(2)]]) {
    ShadowOut out;
    out.position = mvp * applySkinning(in, bones);
    out.uv = in.texCoord;
    return out;
}

// For point lights - uses perspective depth, shader converts for comparison
vertex float4 shadow_point_vertex(ShadowVertexIn in [[stage_in]],
                                  constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex ShadowOut shadow_point_vertex_cutout(ShadowVertexInUV in [[stage_in]],
                                            constant float4x4& mvp [[buffer(1)]]) {
    ShadowOut out;
    out.position = mvp * float4(in.position, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_point_vertex_instanced(ShadowVertexIn in [[stage_in]],
                                            constant float4x4& viewProj [[buffer(1)]],
                                            const device InstanceData* instances [[buffer(2)]],
                                            constant ShadowFoliageParams& foliage [[buffer(3)]],
                                            uint instanceId [[instance_id]]) {
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    return viewProj * float4(worldPos, 1.0);
}

vertex ShadowOut shadow_point_vertex_cutout_instanced(ShadowVertexInUV in [[stage_in]],
                                                      constant float4x4& viewProj [[buffer(1)]],
                                                      const device InstanceData* instances [[buffer(2)]],
                                                      constant ShadowFoliageParams& foliage [[buffer(3)]],
                                                      uint instanceId [[instance_id]]) {
    ShadowOut out;
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    out.position = viewProj * float4(worldPos, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_point_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                          constant float4x4& mvp [[buffer(1)]],
                                          const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}

vertex ShadowOut shadow_point_vertex_cutout_skinned(ShadowVertexInSkinnedUV in [[stage_in]],
                                                    constant float4x4& mvp [[buffer(1)]],
                                                    const device float4x4* bones [[buffer(2)]]) {
    ShadowOut out;
    out.position = mvp * applySkinning(in, bones);
    out.uv = in.texCoord;
    return out;
}

// Area light approximated with projected shadow map.
vertex float4 shadow_area_vertex(ShadowVertexIn in [[stage_in]],
                                 constant float4x4& mvp [[buffer(1)]]) {
    return mvp * float4(in.position, 1.0);
}

vertex ShadowOut shadow_area_vertex_cutout(ShadowVertexInUV in [[stage_in]],
                                           constant float4x4& mvp [[buffer(1)]]) {
    ShadowOut out;
    out.position = mvp * float4(in.position, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_area_vertex_instanced(ShadowVertexIn in [[stage_in]],
                                           constant float4x4& viewProj [[buffer(1)]],
                                           const device InstanceData* instances [[buffer(2)]],
                                           constant ShadowFoliageParams& foliage [[buffer(3)]],
                                           uint instanceId [[instance_id]]) {
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    return viewProj * float4(worldPos, 1.0);
}

vertex ShadowOut shadow_area_vertex_cutout_instanced(ShadowVertexInUV in [[stage_in]],
                                                     constant float4x4& viewProj [[buffer(1)]],
                                                     const device InstanceData* instances [[buffer(2)]],
                                                     constant ShadowFoliageParams& foliage [[buffer(3)]],
                                                     uint instanceId [[instance_id]]) {
    ShadowOut out;
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = foliage.boundsCenter.xyz;
    float3 boundsSize = foliage.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float3 worldPos;
    if (foliage.flags.x > 0.5 && foliage.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffsetShadow(centerWS, foliage);
        float3 toCam = normalize(foliage.cameraTime.xyz - windCenter);
        float3 up = float3(0.0, 1.0, 0.0);
        float3 right = normalize(cross(up, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
    } else {
        worldPos = (inst.modelMatrix * float4(in.position, 1.0)).xyz;
        worldPos = applyWindOffsetShadow(worldPos, foliage);
    }
    out.position = viewProj * float4(worldPos, 1.0);
    out.uv = in.texCoord;
    return out;
}

vertex float4 shadow_area_vertex_skinned(ShadowVertexInSkinned in [[stage_in]],
                                         constant float4x4& mvp [[buffer(1)]],
                                         const device float4x4* bones [[buffer(2)]]) {
    return mvp * applySkinning(in, bones);
}

vertex ShadowOut shadow_area_vertex_cutout_skinned(ShadowVertexInSkinnedUV in [[stage_in]],
                                                   constant float4x4& mvp [[buffer(1)]],
                                                   const device float4x4* bones [[buffer(2)]]) {
    ShadowOut out;
    out.position = mvp * applySkinning(in, bones);
    out.uv = in.texCoord;
    return out;
}
