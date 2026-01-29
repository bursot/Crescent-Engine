#include "Common.metal.h"

struct BakeVertexIn {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
    float3 tangent [[attribute(3)]];
    float3 bitangent [[attribute(4)]];
    float4 color [[attribute(5)]];
};

struct BakeVertexOut {
    float4 position [[position]];
    float2 uv;
    float3 normal;
    float4 color;
};

struct BakeParams {
    float4x4 viewProjection;
    float4 albedo;
    float4 uvTilingOffset;
    float4 lightDirHasTex; // xyz light dir, w hasAlbedoTex
};

vertex BakeVertexOut impostor_bake_vertex(
    BakeVertexIn in [[stage_in]],
    constant BakeParams& params [[buffer(1)]]
) {
    BakeVertexOut out;
    out.position = params.viewProjection * float4(in.position, 1.0);
    out.uv = in.texCoord * params.uvTilingOffset.xy + params.uvTilingOffset.zw;
    out.normal = normalize(in.normal);
    out.color = in.color;
    return out;
}

fragment float4 impostor_bake_fragment(
    BakeVertexOut in [[stage_in]],
    constant BakeParams& params [[buffer(0)]],
    texture2d<float> albedoMap [[texture(0)]],
    sampler texSampler [[sampler(0)]]
) {
    float4 baseColor = params.albedo;
    if (params.lightDirHasTex.w > 0.5) {
        baseColor *= albedoMap.sample(texSampler, in.uv);
    }
    float3 lightDir = normalize(params.lightDirHasTex.xyz);
    float ndotl = max(dot(normalize(in.normal), lightDir), 0.2);
    float3 lit = baseColor.rgb * ndotl;
    return float4(lit, baseColor.a);
}
