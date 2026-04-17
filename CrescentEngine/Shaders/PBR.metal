#include "Common.metal.h"
#include "PBRFunctions.metal.h"

// ============================================================================
// PBR-SPECIFIC STRUCTURES
// ============================================================================

// Vertex input structure
struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
    float3 tangent [[attribute(3)]];
    float3 bitangent [[attribute(4)]];
    float4 color [[attribute(5)]];
    float2 texCoord1 [[attribute(6)]];
};

struct VertexInSkinned {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
    float3 tangent [[attribute(3)]];
    float3 bitangent [[attribute(4)]];
    float4 color [[attribute(5)]];
    float2 texCoord1 [[attribute(6)]];
    uint4 boneIndices [[attribute(7)]];
    float4 boneWeights [[attribute(8)]];
};

// Vertex output / Fragment input
struct VertexOut {
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
    float2 texCoord;
    float2 lightmapTexCoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
    float lodFade;
    float billboardFade;
    float billboardFlag;
    float bakedLightingFlag;
    float staticLightmapFlag;
    float hdrStaticLightmapFlag;
};

struct PrepassOut {
    float4 position [[position]];
    float3 normalVS;
    float2 texCoord;
    float2 lightmapTexCoord;
    float lodFade;
    float billboardFade;
    float billboardFlag;
};

struct VelocityOut {
    float4 position [[position]];
    float4 currClip;
    float4 prevClip;
};

struct BlitVertexOut {
    float4 position [[position]];
    float2 uv;
};

struct SSAOParams {
    float4 settings;     // x radius, y bias, z intensity, w power
    float2 noiseScale;
    float2 texelSize;
};

struct SSAOBlurParams {
    float2 texelSize;
    float depthSharpness;
    float normalSharpness;
};

struct SSRParams {
    float4 settings0; // texelSize.xy, thickness, maxSteps
    float4 settings1; // maxDistance, maxRoughness, fadeStart, fadeEnd
};

struct DecalParams {
    float4 colorOpacity;   // rgb + opacity
    float4 uvTilingOffset; // xy tiling, zw offset
    float4 edgeParams;     // softness, padding
    float4 mapFlags;       // hasAlbedo, hasNormal, hasOrm, hasMask
    float4x4 modelMatrix;
    float4x4 invModel;
};

struct BloomPrefilterParams {
    float threshold;
    float knee;
    float pad0;
    float pad1;
};

struct BloomBlurParams {
    float2 texelSize;
    float2 direction;
};

struct BloomDownsampleParams {
    float2 texelSize;
    float pad0;
    float pad1;
};

struct BloomUpsampleParams {
    float2 texelSize;
    float radius;
    float pad0;
};

struct BloomCombineParams {
    float intensity;
    float pad0;
    float pad1;
    float pad2;
};

struct PostProcessParams {
    float4 params0; // vignetteIntensity, grainIntensity, grainScale, time
    float4 params1; // gradingIntensity, toneMapping, inputIsLinear, padding
};

inline float3 shadowDebugCascadeColor(int cascadeIndex) {
    switch (cascadeIndex) {
        case 0: return float3(1.0, 0.2, 0.2);
        case 1: return float3(0.2, 1.0, 0.2);
        case 2: return float3(0.2, 0.55, 1.0);
        case 3: return float3(1.0, 0.85, 0.2);
        default: return float3(1.0, 0.0, 1.0);
    }
}

inline float3 shadowDebugPointFaceColor(int faceIndex) {
    switch (faceIndex) {
        case 0: return float3(1.0, 0.2, 0.2);
        case 1: return float3(0.2, 1.0, 0.2);
        case 2: return float3(0.2, 0.55, 1.0);
        case 3: return float3(1.0, 0.85, 0.2);
        case 4: return float3(1.0, 0.2, 1.0);
        case 5: return float3(0.2, 1.0, 1.0);
        default: return float3(1.0, 1.0, 1.0);
    }
}

struct TAAParams {
    float4x4 prevViewProjection;
    float4 params0; // texelSize.xy, feedback, historyValid
    float4 params1; // sharpness, useVelocity, depthThreshold, normalThreshold
    float4 params2; // x specular stability enabled, y strength
};

struct MotionBlurParams {
    float4x4 prevViewProjection;
    float4x4 currViewProjection;
    float4 params0; // texelSize.xy, strength, padding
};

struct DofParams {
    float4 params0; // texelSize.xy, focusDistance, aperture
    float4 params1; // maxBlur, padding
};

struct FogParams {
    float4 fogColorDensity; // rgb, density
    float4 distanceParams;  // start, end, height, heightFalloff
    float4 sunDirIntensity; // xyz dir, intensity
    float4 sunColor;        // rgb, padding
    float4 volumeParams;    // near, far, sliceCount, padding
    float4 misc;            // heightFogEnabled, anisotropy, padding, padding
    float4 shadowParams;    // shadowIndex, cascadeCount, enabled, strength
    float4x4 prevViewProjection;
    float4x4 prevViewMatrix;
};

struct VelocityUniforms {
    float4x4 prevModelMatrix;
    float4x4 currViewProjection;
    float4x4 prevViewProjection;
};

// Environment helpers
float3x3 environmentRotation(constant EnvironmentUniforms& env) {
    return float3x3(env.rot0.xyz, env.rot1.xyz, env.rot2.xyz);
}

inline float2 uvToNdc(float2 uv) {
    return float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
}

inline float3 reconstructViewPosition(float2 uv,
                                      float depth,
                                      constant CameraUniforms& camera) {
    float2 ndc = uvToNdc(uv);
    float4 clip = float4(ndc, depth, 1.0);
    float4 view = camera.projectionMatrixInverse * clip;
    float safeW = (abs(view.w) > 1e-5) ? view.w : (view.w < 0.0 ? -1e-5 : 1e-5);
    return view.xyz / safeW;
}

inline float linearizeDepth(float depth, constant CameraUniforms& camera) {
    float a = camera.projectionMatrix[2][2];
    float b = camera.projectionMatrix[3][2];
    float viewZ = b / (-depth - a);
    return -viewZ;
}

inline float computeFade(float dist, float start, float end) {
    float span = end - start;
    if (span <= 0.0001) {
        return 0.0;
    }
    return saturate((dist - start) / span);
}

inline float sampleParallaxHeight(texture2d<float> heightMap,
                                  sampler textureSampler,
                                  float2 uv,
                                  constant MaterialUniforms& material) {
    float height = heightMap.sample(textureSampler, uv).r;
    if (material.textureFlags2.w > 0.5) {
        height = 1.0 - height;
    }
    return height;
}

inline float2 applyParallaxOcclusionMapping(texture2d<float> heightMap,
                                            sampler textureSampler,
                                            float2 baseUV,
                                            float3 viewDirTS,
                                            constant MaterialUniforms& material) {
    const float heightScale = abs(material.heightParams.x);
    if (heightScale <= 1e-5) {
        return baseUV;
    }

    const float viewZ = max(viewDirTS.z, 1e-4);
    const float ndotv = saturate(viewZ);
    const float minLayers = clamp(material.heightParams.y, 8.0, 64.0);
    const float maxLayers = clamp(material.heightParams.z, minLayers, 128.0);
    const float layerCount = clamp(mix(maxLayers, minLayers, ndotv), 8.0, 128.0);
    const float layerDepth = 1.0 / layerCount;

    float2 parallaxDirection = (viewDirTS.xy / viewZ) * heightScale;
    float parallaxLength = length(parallaxDirection);
    if (parallaxLength > 0.25) {
        parallaxDirection *= 0.25 / parallaxLength;
    }

    float2 deltaUV = parallaxDirection / layerCount;
    float2 currentUV = baseUV;
    float currentDepth = 0.0;
    float currentHeight = sampleParallaxHeight(heightMap, textureSampler, currentUV, material);

    float2 previousUV = currentUV;
    float previousDepth = currentDepth;
    float previousHeight = currentHeight;

    for (int layer = 0; layer < 128; ++layer) {
        if (layer >= int(layerCount) || currentDepth >= currentHeight) {
            break;
        }
        previousUV = currentUV;
        previousDepth = currentDepth;
        previousHeight = currentHeight;
        currentUV -= deltaUV;
        currentDepth += layerDepth;
        currentHeight = sampleParallaxHeight(heightMap, textureSampler, currentUV, material);
    }

    float before = previousHeight - previousDepth;
    float after = currentHeight - currentDepth;
    float weightDenom = before - after;
    float weight = (abs(weightDenom) > 1e-5) ? clamp(before / weightDenom, 0.0, 1.0) : 0.0;
    float2 refinedUV = mix(previousUV, currentUV, weight);

    float2 lowUV = previousUV;
    float2 highUV = currentUV;
    float lowDepth = previousDepth;
    float highDepth = currentDepth;

    for (int i = 0; i < 5; ++i) {
        float2 midUV = (lowUV + highUV) * 0.5;
        float midDepth = (lowDepth + highDepth) * 0.5;
        float midHeight = sampleParallaxHeight(heightMap, textureSampler, midUV, material);
        if (midDepth < midHeight) {
            lowUV = midUV;
            lowDepth = midDepth;
        } else {
            highUV = midUV;
            highDepth = midDepth;
        }
    }

    return (refinedUV + (lowUV + highUV) * 0.5) * 0.5;
}

inline float integrateHeightFog(float baseDensity,
                                float fogHeight,
                                float heightFalloff,
                                float rayOriginY,
                                float rayDirY,
                                float startDist,
                                float endDist) {
    float segmentLength = max(endDist - startDist, 0.0);
    if (segmentLength <= 0.0001 || baseDensity <= 0.0) {
        return 0.0;
    }

    float originTerm = exp(clamp(-heightFalloff * (rayOriginY - fogHeight), -32.0, 32.0));
    float k = heightFalloff * rayDirY;
    if (abs(k) < 0.0001) {
        return baseDensity * originTerm * segmentLength;
    }

    float startTerm = exp(clamp(-k * startDist, -32.0, 32.0));
    float endTerm = exp(clamp(-k * endDist, -32.0, 32.0));
    return baseDensity * originTerm * (startTerm - endTerm) / k;
}

inline float2 impostorAtlasUV(float2 baseUV,
                              float3 toCamDir,
                              float3x3 worldToLocal,
                              constant MaterialUniforms& material) {
    if (material.impostorParams0.x < 0.5) {
        return baseUV;
    }
    float rows = max(material.impostorParams0.y, 1.0);
    float cols = max(material.impostorParams0.z, 1.0);
    float3 localDir = normalize(worldToLocal * toCamDir);
    float azimuth = atan2(localDir.x, localDir.z);
    float elevation = asin(clamp(localDir.y, -1.0, 1.0));
    float u = azimuth / TWO_PI + 0.5;
    float v = 1.0 - (elevation / PI + 0.5);
    float col = clamp(floor(u * cols), 0.0, cols - 1.0);
    float row = clamp(floor(v * rows), 0.0, rows - 1.0);
    float2 scale = float2(1.0 / cols, 1.0 / rows);
    float2 offset = float2(col, row) * scale;
    return baseUV * scale + offset;
}

inline float3 applyWindOffset(float3 worldPos,
                              float weight,
                              constant MaterialUniforms& material,
                              constant CameraUniforms& camera) {
    if (material.foliageParams2.x < 0.5 || material.foliageParams0.x <= 0.0001) {
        return worldPos;
    }
    float3 dir = material.foliageParams3.xyz;
    if (dot(dir, dir) < 0.0001) {
        dir = float3(1.0, 0.0, 0.0);
    }
    dir = normalize(dir);
    float time = camera.cameraPositionTime.w;
    float phase = dot(worldPos.xz, dir.xz) * material.foliageParams0.z + time * material.foliageParams0.y;
    float sway = sin(phase) * material.foliageParams0.x;
    float gust = sin(phase * 0.7 + time * material.foliageParams0.y * 0.5) * material.foliageParams0.w;
    float offset = (sway + gust) * weight;
    return worldPos + dir * offset;
}

inline float3 normalizedTerrainWeights(float3 weights, float sharpness) {
    weights = max(weights, float3(0.0));
    weights = pow(weights, float3(max(sharpness, 0.1)));
    float sum = weights.x + weights.y + weights.z;
    if (sum <= 0.0001) {
        return float3(1.0, 0.0, 0.0);
    }
    return weights / sum;
}

inline float3 computeTerrainWeights(float2 uv,
                                    float3 worldPos,
                                    float3 normalWS,
                                    constant MaterialUniforms& material,
                                    texture2d<float> controlMap,
                                    sampler textureSampler) {
    float2 controlUV = clamp(uv, float2(0.0), float2(1.0));
    float3 weights = float3(1.0, 0.0, 0.0);
    if (material.terrainFlags.x > 0.5) {
        weights = controlMap.sample(textureSampler, controlUV).rgb;
        return normalizedTerrainWeights(weights, material.terrainParams0.y);
    }

    float heightStart = material.terrainParams0.z;
    float heightEnd = material.terrainParams0.w;
    float heightSpan = max(heightEnd - heightStart, 0.001);
    float h = saturate((worldPos.y - heightStart) / heightSpan);

    float w0 = 1.0 - h;
    float w2 = h;
    float w1 = 1.0 - abs(2.0 * h - 1.0);

    float slope = saturate(1.0 - abs(normalWS.y));
    float slopeStart = material.terrainParams1.x;
    float slopeEnd = max(material.terrainParams1.y, slopeStart + 0.001);
    float steep = smoothstep(slopeStart, slopeEnd, slope);

    w0 *= (1.0 - steep);
    w1 *= (1.0 - steep * 0.5);
    w2 = max(w2, steep);

    weights = float3(w0, w1, w2);
    return normalizedTerrainWeights(weights, material.terrainParams0.y);
}

inline float henyeyGreensteinPhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001), 1.5);
    return (1.0 - g2) / max(4.0 * PI * denom, 0.0001);
}

inline float3 sampleLUT2D(float3 color, texture2d<float> lutTex, sampler texSampler) {
    color = clamp(color, 0.0, 1.0);
    float size = max(float(lutTex.get_height()), 2.0);
    float slice = color.b * (size - 1.0);
    float slice0 = floor(slice);
    float slice1 = min(slice0 + 1.0, size - 1.0);
    float zLerp = slice - slice0;
    float width = float(lutTex.get_width());
    float2 uv0 = float2((color.r * (size - 1.0) + slice0 * size + 0.5) / width,
                        (color.g * (size - 1.0) + 0.5) / size);
    float2 uv1 = float2((color.r * (size - 1.0) + slice1 * size + 0.5) / width,
                        (color.g * (size - 1.0) + 0.5) / size);
    float3 c0 = lutTex.sample(texSampler, uv0, level(0.0)).rgb;
    float3 c1 = lutTex.sample(texSampler, uv1, level(0.0)).rgb;
    return mix(c0, c1, zLerp);
}

inline float3 tonemapFilmic(float3 color) {
    color = color / (color + float3(0.6));
    color = color * 1.4;
    color = pow(color, float3(1.0/2.2));
    return color;
}

inline float3 tonemapACES(float3 color) {
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;
    color = clamp(color, 0.0, 1.0);
    color = pow(color, float3(1.0/2.2));
    return color;
}

inline float3 applyDither(float3 color, float2 pixel, float amount) {
    float2 p = floor(pixel);
    float noise = fract(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
    float dither = (noise - 0.5) * (amount / 255.0);
    return clamp(color + dither, 0.0, 1.0);
}

inline float luminance709(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}



float3 applyEnvironmentGrading(float3 color, constant EnvironmentUniforms& env) {
    color *= env.colorControl.rgb;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = mix(float3(luminance), color, env.colorControl.w);      // saturation
    color = mix(float3(0.5), color, env.toneControl.x);             // contrast around mid-grey
    return color;
}

float3 sampleEnvironment(texture2d<float> envMap,
                         sampler envSampler,
                         float3 direction,
                         float lod,
                         constant EnvironmentUniforms& env) {
    float3x3 rot = environmentRotation(env);
    float3 rotated = rot * normalize(direction);
    float2 uv = directionToEquirectUV(rotated);
    uv.x = fract(uv.x);
    uv.y = clamp(uv.y, 0.0, 1.0);
    float lodLevel = max(lod + env.exposureIntensity.w, 0.0);
    float3 color = envMap.sample(envSampler, uv, level(lodLevel)).rgb;
    return applyEnvironmentGrading(color, env);
}

// ========================================================================
// SHADOW SAMPLING HELPERS
// ========================================================================

constant float2 kPoissonDisk16[16] = {
    float2(-0.94201624, -0.39906216),
    float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870),
    float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),
    float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554),
    float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),
    float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367),
    float2(0.14383161, -0.14100790)
};

constant float3 kSSAOKernel[16] = {
    float3(0.5381, 0.1856, 0.4319),
    float3(0.1379, 0.2486, 0.4430),
    float3(0.3371, 0.5679, 0.0057),
    float3(-0.6999, -0.0451, 0.0019),
    float3(0.0689, -0.1598, 0.8547),
    float3(0.0560, 0.0069, 0.1843),
    float3(-0.0146, 0.1402, 0.0762),
    float3(0.0100, -0.1924, 0.0344),
    float3(-0.3577, -0.5301, 0.4358),
    float3(-0.3169, 0.1063, 0.0158),
    float3(0.0103, -0.5869, 0.0046),
    float3(-0.0897, -0.4940, 0.3287),
    float3(0.7119, -0.0154, 0.0918),
    float3(-0.0533, 0.0596, 0.5411),
    float3(0.0352, -0.0631, 0.5460),
    float3(-0.4776, 0.2847, 0.0271)
};

inline float hash21(float2 p) {
    return fract(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

constant bool kDebugPointShadowView = false;

inline float2 rotate2(float2 v, float2 r) {
    return float2(v.x * r.x - v.y * r.y, v.x * r.y + v.y * r.x);
}

inline uint dominantCubeFace(float3 v) {
    float3 a = abs(v);
    if (a.x >= a.y && a.x >= a.z) {
        return (v.x >= 0.0) ? 0u : 1u;
    }
    if (a.y >= a.z) {
        return (v.y >= 0.0) ? 2u : 3u;
    }
    return (v.z >= 0.0) ? 4u : 5u;
}

struct PointShadowProjection {
    uint face;
    float2 uv;
    float forwardDist;
};

inline PointShadowProjection projectPointShadowFace(float3 toFrag) {
    PointShadowProjection result;
    result.face = dominantCubeFace(toFrag);

    float3 forward = float3(0.0);
    float3 right = float3(0.0);
    float3 up = float3(0.0);

    switch (result.face) {
        case 0u:
            forward = float3(1.0, 0.0, 0.0);
            right = float3(0.0, 0.0, -1.0);
            up = float3(0.0, -1.0, 0.0);
            break;
        case 1u:
            forward = float3(-1.0, 0.0, 0.0);
            right = float3(0.0, 0.0, 1.0);
            up = float3(0.0, -1.0, 0.0);
            break;
        case 2u:
            forward = float3(0.0, 1.0, 0.0);
            right = float3(1.0, 0.0, 0.0);
            up = float3(0.0, 0.0, 1.0);
            break;
        case 3u:
            forward = float3(0.0, -1.0, 0.0);
            right = float3(1.0, 0.0, 0.0);
            up = float3(0.0, 0.0, -1.0);
            break;
        case 4u:
            forward = float3(0.0, 0.0, 1.0);
            right = float3(1.0, 0.0, 0.0);
            up = float3(0.0, -1.0, 0.0);
            break;
        default:
            forward = float3(0.0, 0.0, -1.0);
            right = float3(-1.0, 0.0, 0.0);
            up = float3(0.0, -1.0, 0.0);
            break;
    }

    result.forwardDist = max(dot(toFrag, forward), 1e-5);
    float2 ndc = float2(dot(toFrag, right), dot(toFrag, up)) / result.forwardDist;
    result.uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    return result;
}

inline float sampleShadowDepthPCF(depth2d<float> atlas,
                                  sampler shadowSampler,
                                  float2 uv,
                                  float depth,
                                  float bias,
                                  float2 tileMin,
                                  float2 tileMax,
                                  float2 rot,
                                  float radius) {
    constexpr sampler shadowDepthSampler(filter::nearest, address::clamp_to_edge);
    float2 texel = 1.0 / float2(atlas.get_width(), atlas.get_height());
    float2 margin = texel * (radius + 1.0);
    float2 minBounds = tileMin + margin;
    float2 maxBounds = tileMax - margin;
    float shadow = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        float2 offset = rotate2(kPoissonDisk16[i], rot) * radius;
        float2 uvOffset = uv + offset * texel;
        if (uvOffset.x >= minBounds.x && uvOffset.x <= maxBounds.x &&
            uvOffset.y >= minBounds.y && uvOffset.y <= maxBounds.y) {
            float sampleDepth = atlas.sample(shadowDepthSampler, uvOffset);
            float w = 1.0 - saturate(length(kPoissonDisk16[i]) * 0.5);
            shadow += ((depth - bias <= sampleDepth) ? 1.0 : 0.0) * w;
            weightSum += w;
        }
    }
    return (weightSum > 0.0) ? (shadow / weightSum) : 1.0;
}

float pcfSampleManual(depth2d<float> atlas,
                      float2 uv,
                      float depth,
                      float bias,
                      float2 tileMin,
                      float2 tileMax,
                      float2 rot,
                      float radius) {
    constexpr sampler shadowDepthSampler(filter::nearest, address::clamp_to_edge);
    float2 texel = 1.0 / float2(atlas.get_width(), atlas.get_height());
    float shadow = 0.0;
    float weightSum = 0.0;

    // Add small margin to prevent atlas tile bleeding.
    float2 margin = texel * (radius + 1.0);
    tileMin += margin;
    tileMax -= margin;

    for (int i = 0; i < 16; ++i) {
        float2 offset = rotate2(kPoissonDisk16[i], rot) * radius;
        float2 uvOffset = uv + offset * texel;
        if (uvOffset.x >= tileMin.x && uvOffset.x <= tileMax.x &&
            uvOffset.y >= tileMin.y && uvOffset.y <= tileMax.y) {
            float w = 1.0 - saturate(length(kPoissonDisk16[i]) * 0.35);
            float sampleDepth = atlas.sample(shadowDepthSampler, uvOffset);
            shadow += ((depth - bias <= sampleDepth) ? 1.0 : 0.0) * w;
            weightSum += w;
        }
    }

    return (weightSum > 0.0) ? (shadow / weightSum) : 1.0;
}

float pcssSample(depth2d<float> atlas,
                 sampler shadowSampler,
                 float2 uv,
                 float depth,
                 float bias,
                 float2 tileMin,
                 float2 tileMax,
                 float penumbra,
                 float2 rot,
                 float baseRadius) {
    constexpr sampler shadowDepthSampler(filter::nearest, address::clamp_to_edge);
    float2 texel = 1.0 / float2(atlas.get_width(), atlas.get_height());
    float searchRadius = max(baseRadius * 2.0, 2.0);
    float2 margin = texel * (searchRadius + 1.0);
    float2 minBounds = tileMin + margin;
    float2 maxBounds = tileMax - margin;

    // Blocker search
    float blockerDepth = 0.0;
    float blockers = 0.0;
    for (int i = 0; i < 16; ++i) {
        float2 offset = rotate2(kPoissonDisk16[i], rot) * searchRadius;
        float2 offsetUV = uv + offset * texel;
        if (offsetUV.x >= minBounds.x && offsetUV.x <= maxBounds.x &&
            offsetUV.y >= minBounds.y && offsetUV.y <= maxBounds.y) {
            float sampleDepth = atlas.sample(shadowDepthSampler, offsetUV);
            if (sampleDepth < depth - bias) {
                blockerDepth += sampleDepth;
                blockers += 1.0;
            }
        }
    }
    if (blockers < 0.5) {
        return 1.0;
    }

    float avgBlocker = blockerDepth / blockers;
    float penumbraRatio = saturate((depth - avgBlocker) / max(avgBlocker, 0.0001));
    float radius = clamp(baseRadius + penumbraRatio * penumbra * 24.0, baseRadius, 32.0);
    return pcfSampleManual(atlas, uv, depth, bias, tileMin, tileMax, rot, radius);
}

inline float sampleShadowCascade(const device ShadowGPUData* shadowData,
                                 int idx,
                                 float3 pos,
                                 float3 normalWS,
                                 float3 lightDirWS,
                                 depth2d<float> atlas,
                                 sampler shadowSampler,
                                 bool usePCSS,
                                 bool useContact,
                                 float baseTexelWorld,
                                 bool isCascade) {
    ShadowGPUData s = shadowData[idx];
    float3 n = normalize(normalWS);
    float nDotLPre = saturate(dot(n, normalize(lightDirWS)));
    float receiverScale = isCascade
        ? mix(0.72, 0.22, nDotLPre)
        : mix(1.2, 0.4, nDotLPre);
    // Keep directional cascades reasonably consistent across splits, but do
    // not force every split to use the finest cascade texel size. That was
    // under-biasing far cascades and produced blocky acne on flat receivers.
    float receiverTexelWorld = s.depthRange.z;
    if (isCascade) {
        receiverTexelWorld = min(s.depthRange.z, max(baseTexelWorld * 2.0, baseTexelWorld));
    }
    float receiverOffset = max(receiverTexelWorld, 1e-4) * receiverScale;
    float3 samplePos = pos + n * receiverOffset;
    float4 clip = s.viewProj * float4(samplePos, 1.0);
    float3 ndc = clip.xyz / clip.w;
    // Right-handed projection: X,Y in [-1,1], Z in [0,1]
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }
    // Convert NDC to texture UV [0,1]
    // Metal texture origin is top-left, NDC Y-axis is bottom-to-top
    // So we need to flip Y: NDC Y=+1 (top) -> UV V=0 (top), NDC Y=-1 (bottom) -> UV V=1 (bottom)
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    // Scale and offset to atlas tile
    uv = uv * s.atlasUV.zw + s.atlasUV.xy;
    // Check if UV is within the tile bounds in the atlas
    float2 tileMin = s.atlasUV.xy;
    float2 tileMax = s.atlasUV.xy + s.atlasUV.zw;
    if (uv.x < tileMin.x || uv.x > tileMax.x || uv.y < tileMin.y || uv.y > tileMax.y) {
        return 1.0;
    }
    float bias = s.params.x;
    float nDotL = nDotLPre;
    float slope = 1.0 - nDotL;
    // Keep a small baseline normal-bias even away from grazing angles.
    // This reduces acne/shimmer without requiring extreme inspector values.
    float normalBiasScale = isCascade ? (0.65 + 0.75 * slope) : (0.45 + 0.95 * slope);
    bias += s.params.y * normalBiasScale;

    float2 rot = float2(1.0, 0.0);
    if (usePCSS && !isCascade && (s.params.w > 0.5)) {
        // World-anchored rotation: stable when camera moves, breaks visible tiling.
        float2 worldCell = floor(pos.xz / max(s.depthRange.z * 8.0, 0.05));
        float seed = hash21(worldCell + s.atlasUV.xy * 4096.0 + (s.params.w + 1.0));
        float randAngle = fract(sin(seed) * 43758.5453) * TWO_PI;
        rot = float2(cos(randAngle), sin(randAngle));
    }

    // Grow filter footprint mildly with texel size, but keep cascades sharp.
    float cascadeTexelWorld = max(s.depthRange.z, 1e-5);
    float texelRatio = clamp(cascadeTexelWorld / max(baseTexelWorld, 1e-5), 1.0, isCascade ? 1.25 : 3.0);
    float kernelBase = isCascade ? 0.85 : ((s.params.w < 0.5) ? 1.2 : 1.8);
    float kernelRadius = kernelBase * sqrt(texelRatio);

    // ndc.z is already 0..1 for our D3D/Metal-style projections
    float depth = ndc.z;

    float penumbra = s.params.z;
    float shadow = 1.0;
    bool usePCSSLocal = usePCSS && (s.params.w > 0.5);
    if (isCascade) {
        if (!usePCSS) {
            // Keep hard shadows atlas-tile safe as well.
            shadow = sampleShadowDepthPCF(atlas, shadowSampler, uv, depth, bias, tileMin, tileMax, float2(1.0, 0.0), 0.0);
        } else {
            float cascadeRadius = clamp(kernelRadius, 0.7, 2.2);
            shadow = pcfSampleManual(atlas, uv, depth, bias, tileMin, tileMax, rot, cascadeRadius);
        }
    } else if (usePCSSLocal && penumbra > 0.0) {
        shadow = pcssSample(atlas, shadowSampler, uv, depth, bias, tileMin, tileMax, penumbra, rot, kernelRadius);
    } else {
        shadow = pcfSampleManual(atlas, uv, depth, bias, tileMin, tileMax, rot, kernelRadius);
    }

    if (useContact) {
        float contactBias = bias * 0.5;
        float contactRadius = 1.0;
        float contact = pcfSampleManual(atlas, uv, depth, contactBias, tileMin, tileMax, rot, contactRadius);
        float contactStrength = isCascade ? 0.35 : ((s.params.w < 0.5) ? 0.4 : 0.6);
        shadow = min(shadow, mix(1.0, contact, contactStrength));
    }

    return shadow;
}

float sampleShadow(const device ShadowGPUData* shadowData,
                   int shadowIdx,
                   int cascadeCount,
                   float viewDepth,
                   float3 worldPos,
                   float3 normalWS,
                   float3 lightDirWS,
                   depth2d<float> atlas,
                   sampler shadowSampler,
                   bool usePCSS,
                   bool useContact) {
    if (shadowIdx < 0) return 1.0;
    float baseTexelWorld = max(shadowData[shadowIdx].depthRange.z, 1e-5);
    bool usePCSSForThisLight = usePCSS;

    if (cascadeCount <= 1) {
        return sampleShadowCascade(shadowData, shadowIdx, worldPos, normalWS, lightDirWS, atlas, shadowSampler, usePCSSForThisLight, useContact, baseTexelWorld, false);
    }

    float farLimit = shadowData[shadowIdx + cascadeCount - 1].depthRange.y;
    if (viewDepth > farLimit) {
        return 1.0;
    }

    // Use view-space depth for cascade selection (matches split computation)
    viewDepth = max(viewDepth, 0.0);

    // Find the appropriate cascade using explicit [near, far] intervals.
    int current = -1;
    for (int c = 0; c < cascadeCount; ++c) {
        ShadowGPUData s = shadowData[shadowIdx + c];
        if (viewDepth >= s.depthRange.x && viewDepth <= s.depthRange.y) {
            current = c;
            break;
        }
        if (viewDepth < s.depthRange.x) {
            current = c;
            break;
        }
    }
    if (current < 0) {
        return 1.0;
    }

    // Blend with next cascade for smooth transitions
    int cascadeA = current;
    int cascadeB = current;
    float blend = 0.0;

    if (current < cascadeCount - 1) {
        ShadowGPUData curData = shadowData[shadowIdx + current];

        // Keep a wider transition to reduce visible cascade edge shimmer during camera motion.
        float cascadeRange = max(curData.depthRange.y - curData.depthRange.x, 0.001);
        float blendFactor = 0.18;
        float blendRange = blendFactor * cascadeRange;
        float blendStart = curData.depthRange.y - blendRange;

        if (viewDepth > blendStart) {
            cascadeB = current + 1;
            float t = saturate((viewDepth - blendStart) / max(blendRange, 1e-4));
            blend = smoothstep(0.0, 1.0, t);
        }
    }

    bool allowPCSS_A = usePCSSForThisLight;
    float shadowA = sampleShadowCascade(shadowData, shadowIdx + cascadeA, worldPos, normalWS, lightDirWS, atlas, shadowSampler, allowPCSS_A, useContact, baseTexelWorld, true);
    if (blend < 0.001) return shadowA;  // Skip second sample if blend is negligible
    bool allowPCSS_B = usePCSSForThisLight;
    float shadowB = sampleShadowCascade(shadowData, shadowIdx + cascadeB, worldPos, normalWS, lightDirWS, atlas, shadowSampler, allowPCSS_B, useContact, baseTexelWorld, true);
    return mix(shadowA, shadowB, blend);
}

inline int resolveDirectionalCascadeIndex(const device ShadowGPUData* shadowData,
                                          int shadowIdx,
                                          int cascadeCount,
                                          float viewDepth) {
    if (cascadeCount <= 1) {
        return 0;
    }
    int current = cascadeCount - 1;
    for (int c = 0; c < cascadeCount; ++c) {
        ShadowGPUData s = shadowData[shadowIdx + c];
        if (viewDepth <= s.depthRange.y) {
            current = c;
            break;
        }
    }
    return current;
}

float samplePointShadow(const device ShadowGPUData* shadowData,
                        int shadowIdx,
                        float3 worldPos,
                        float3 normalWS,
                        float3 lightPosWS,
                        float3 lightDirWS,
                        float tier,
                        depth2d_array<float> cube0,
                        depth2d_array<float> cube1,
                        depth2d_array<float> cube2,
                        depth2d_array<float> cube3,
                        sampler shadowSampler) {
    constexpr sampler shadowFaceDepthSampler(filter::nearest, address::clamp_to_edge);
    ShadowGPUData s = shadowData[shadowIdx];
    float nearP = s.depthRange.x;
    float farP = s.depthRange.y;
    float3 toLight = lightPosWS - worldPos;
    float nDotL = saturate(dot(normalize(normalWS), normalize(toLight)));
    uint cubeIndex = (uint)round(s.depthRange.z);
    int t = (int)round(tier);
    depth2d_array<float> cubeTex = (t == 1) ? cube1 : (t == 2 ? cube2 : (t == 3 ? cube3 : cube0));
    float texelWorld = (2.0 * farP) / max(float(cubeTex.get_width()), 1.0);
    float receiverOffset = texelWorld * mix(2.35, 0.85, nDotL);
    float3 samplePos = worldPos + normalize(normalWS) * receiverOffset;
    float3 toFrag = samplePos - lightPosWS;
    PointShadowProjection proj = projectPointShadowFace(toFrag);
    uint slice = cubeIndex * 6u + proj.face;

    float ref = saturate((length(toFrag) - nearP) / max(farP - nearP, 1e-5));

    float bias = s.params.x;
    bias += s.params.y * (0.65 + 1.1 * (1.0 - nDotL));
    ref = max(ref - bias, 0.0);

    float2 texel = 1.0 / float2(cubeTex.get_width(), cubeTex.get_height());
    float2 worldCell = floor(samplePos.xz / max(texelWorld * 6.0, 0.05));
    float seed = hash21(worldCell + float2((float)cubeIndex * 7.0 + (float)proj.face,
                                           lightPosWS.x * 0.13 + lightPosWS.z * 0.07));
    float randAngle = fract(sin(seed) * 43758.5453) * TWO_PI;
    float2 rot = float2(cos(randAngle), sin(randAngle));
    float kernelRadius = clamp(1.35 + s.params.z * 1.15, 1.25, 3.25);

    // Use a world-anchored Poisson kernel instead of an axis-aligned 3x3 grid.
    // The old kernel produced very visible rectangular shimmer on flat surfaces.
    float shadow = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < 16; ++i) {
        float2 offset = rotate2(kPoissonDisk16[i], rot) * kernelRadius;
        float2 uv = clamp(proj.uv + offset * texel, texel * 0.5, 1.0 - texel * 0.5);
        float sampleDepth = cubeTex.sample(shadowFaceDepthSampler, uv, slice);
        float w = 1.0 - saturate(length(kPoissonDisk16[i]) * 0.35);
        shadow += ((ref <= sampleDepth) ? 1.0 : 0.0) * w;
        weightSum += w;
    }
    return (weightSum > 0.0) ? (shadow / weightSum) : 1.0;
}

// ============================================================================
// VERTEX SHADER
// ============================================================================

vertex PrepassOut vertex_prepass(
    VertexIn in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    constant MaterialUniforms& material [[buffer(3)]],
    constant MeshUniforms& mesh [[buffer(4)]]
) {
    PrepassOut out;
    float3 boundsCenter = mesh.boundsCenter.xyz;
    float3 boundsSize = mesh.boundsSize.xyz;
    float3 centerWS = (model.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float dist = distance(camera.cameraPositionTime.xyz, centerWS);
    float lodFade = (material.foliageParams2.y > 0.5) ? computeFade(dist, material.foliageParams1.x, material.foliageParams1.y) : 0.0;
    float billboardFade = (material.foliageParams2.z > 0.5) ? computeFade(dist, material.foliageParams1.z, material.foliageParams1.w) : 0.0;

    float weight = saturate(in.color.a);
    float3 worldNormal = float3(0.0, 1.0, 0.0);
    float3 worldPos = float3(0.0);
    if (mesh.flags.x > 0.5 && material.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffset(centerWS, weight, material, camera);
        float3 toCam = normalize(camera.cameraPositionTime.xyz - windCenter);
        float3 upRef = (abs(toCam.y) < 0.98) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        float3 right = normalize(cross(upRef, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = model.modelMatrix[0].xyz;
        float3 axisY = model.modelMatrix[1].xyz;
        float3 axisZ = model.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
        worldNormal = normalize(toCam);
    } else {
        float4 wp = model.modelMatrix * float4(in.position, 1.0);
        wp.xyz = applyWindOffset(wp.xyz, weight, material, camera);
        worldPos = wp.xyz;
        worldNormal = normalize((model.normalMatrix * float4(in.normal, 0.0)).xyz);
    }

    out.position = camera.viewProjectionMatrix * float4(worldPos, 1.0);
    out.normalVS = normalize((camera.viewMatrix * float4(worldNormal, 0.0)).xyz);
    out.texCoord = in.texCoord;
    out.lightmapTexCoord = in.texCoord1 * mesh.lightmapScaleOffset.xy + mesh.lightmapScaleOffset.zw;
    out.lodFade = lodFade;
    out.billboardFade = billboardFade;
    out.billboardFlag = mesh.flags.x;
    return out;
}

vertex PrepassOut vertex_prepass_instanced(
    VertexIn in [[stage_in]],
    const device InstanceData* instances [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    constant MaterialUniforms& material [[buffer(3)]],
    constant MeshUniforms& mesh [[buffer(4)]],
    uint instanceId [[instance_id]]
) {
    PrepassOut out;
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = mesh.boundsCenter.xyz;
    float3 boundsSize = mesh.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float dist = distance(camera.cameraPositionTime.xyz, centerWS);
    float lodFade = (material.foliageParams2.y > 0.5) ? computeFade(dist, material.foliageParams1.x, material.foliageParams1.y) : 0.0;
    float billboardFade = (material.foliageParams2.z > 0.5) ? computeFade(dist, material.foliageParams1.z, material.foliageParams1.w) : 0.0;

    float weight = saturate(in.color.a);
    float3 worldNormal = float3(0.0, 1.0, 0.0);
    float3 worldPos = float3(0.0);
    if (mesh.flags.x > 0.5 && material.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffset(centerWS, weight, material, camera);
        float3 toCam = normalize(camera.cameraPositionTime.xyz - windCenter);
        float3 upRef = (abs(toCam.y) < 0.98) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        float3 right = normalize(cross(upRef, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
        worldNormal = normalize(toCam);
    } else {
        float4 wp = inst.modelMatrix * float4(in.position, 1.0);
        wp.xyz = applyWindOffset(wp.xyz, weight, material, camera);
        worldPos = wp.xyz;
        worldNormal = normalize((inst.normalMatrix * float4(in.normal, 0.0)).xyz);
    }

    out.position = camera.viewProjectionMatrix * float4(worldPos, 1.0);
    out.normalVS = normalize((camera.viewMatrix * float4(worldNormal, 0.0)).xyz);
    out.texCoord = in.texCoord;
    out.lightmapTexCoord = in.texCoord1 * mesh.lightmapScaleOffset.xy + mesh.lightmapScaleOffset.zw;
    out.lodFade = lodFade;
    out.billboardFade = billboardFade;
    out.billboardFlag = mesh.flags.x;
    return out;
}

vertex PrepassOut vertex_prepass_skinned(
    VertexInSkinned in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    const device float4x4* bones [[buffer(3)]]
) {
    PrepassOut out;
    float4x4 skin = float4x4(1.0);
    float totalWeight = in.boneWeights.x + in.boneWeights.y + in.boneWeights.z + in.boneWeights.w;
    if (totalWeight > 0.0) {
        float4 weights = in.boneWeights / totalWeight;
        skin = bones[in.boneIndices.x] * weights.x +
               bones[in.boneIndices.y] * weights.y +
               bones[in.boneIndices.z] * weights.z +
               bones[in.boneIndices.w] * weights.w;
    }

    float4 skinnedPos = skin * float4(in.position, 1.0);
    float4 worldPos = model.modelMatrix * skinnedPos;
    out.position = camera.viewProjectionMatrix * worldPos;

    float3x3 skin3 = float3x3(skin[0].xyz, skin[1].xyz, skin[2].xyz);
    float3 worldNormal = normalize((model.normalMatrix * float4(skin3 * in.normal, 0.0)).xyz);
    out.normalVS = normalize((camera.viewMatrix * float4(worldNormal, 0.0)).xyz);
    out.texCoord = in.texCoord;
    out.lightmapTexCoord = in.texCoord1;
    out.lodFade = 0.0;
    out.billboardFade = 0.0;
    out.billboardFlag = 0.0;
    return out;
}

vertex VelocityOut vertex_velocity(
    VertexIn in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    constant MaterialUniforms& material [[buffer(3)]],
    constant MeshUniforms& mesh [[buffer(4)]],
    constant VelocityUniforms& velocity [[buffer(5)]]
) {
    VelocityOut out;
    float weight = saturate(in.color.a);
    float3 worldPos = float3(0.0);
    float3 prevWorldPos = float3(0.0);
    if (mesh.flags.x > 0.5 && material.foliageParams2.z > 0.5) {
        float3 boundsCenter = mesh.boundsCenter.xyz;
        float3 boundsSize = mesh.boundsSize.xyz;
        float3 centerWS = (model.modelMatrix * float4(boundsCenter, 1.0)).xyz;
        float3 prevCenterWS = (velocity.prevModelMatrix * float4(boundsCenter, 1.0)).xyz;
        float3 windCenter = applyWindOffset(centerWS, weight, material, camera);
        float3 prevWindCenter = applyWindOffset(prevCenterWS, weight, material, camera);
        float3 toCam = normalize(camera.cameraPositionTime.xyz - windCenter);
        float3 prevToCam = normalize(camera.cameraPositionTime.xyz - prevWindCenter);
        float3 upRef = (abs(toCam.y) < 0.98) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        float3 prevUpRef = (abs(prevToCam.y) < 0.98) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        float3 right = normalize(cross(upRef, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 prevRight = normalize(cross(prevUpRef, prevToCam));
        float3 prevBillUp = normalize(cross(prevToCam, prevRight));
        float3 axisX = model.modelMatrix[0].xyz;
        float3 axisY = model.modelMatrix[1].xyz;
        float3 axisZ = model.modelMatrix[2].xyz;
        float3 prevAxisX = velocity.prevModelMatrix[0].xyz;
        float3 prevAxisY = velocity.prevModelMatrix[1].xyz;
        float3 prevAxisZ = velocity.prevModelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float prevUniformScale = max(length(prevAxisX), max(length(prevAxisY), length(prevAxisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float prevWidth = max(boundsSize.x, boundsSize.z) * prevUniformScale;
        float prevHeight = max(boundsSize.y, 0.0001) * prevUniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
        prevWorldPos = prevWindCenter + prevRight * (quad.x * prevWidth) + prevBillUp * (quad.y * prevHeight);
    } else {
        float4 wp = model.modelMatrix * float4(in.position, 1.0);
        float4 prevWp = velocity.prevModelMatrix * float4(in.position, 1.0);
        wp.xyz = applyWindOffset(wp.xyz, weight, material, camera);
        prevWp.xyz = applyWindOffset(prevWp.xyz, weight, material, camera);
        worldPos = wp.xyz;
        prevWorldPos = prevWp.xyz;
    }
    float4 currClip = velocity.currViewProjection * float4(worldPos, 1.0);
    float4 prevClip = velocity.prevViewProjection * float4(prevWorldPos, 1.0);
    out.position = currClip;
    out.currClip = currClip;
    out.prevClip = prevClip;
    return out;
}

vertex VelocityOut vertex_velocity_skinned(
    VertexInSkinned in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    const device float4x4* bones [[buffer(3)]],
    constant VelocityUniforms& velocity [[buffer(5)]],
    const device float4x4* prevBones [[buffer(6)]],
    constant MaterialUniforms& material [[buffer(7)]]
) {
    VelocityOut out;
    float4x4 skin = float4x4(1.0);
    float4x4 skinPrev = float4x4(1.0);
    float totalWeight = in.boneWeights.x + in.boneWeights.y + in.boneWeights.z + in.boneWeights.w;
    if (totalWeight > 0.0) {
        float4 weights = in.boneWeights / totalWeight;
        skin = bones[in.boneIndices.x] * weights.x +
               bones[in.boneIndices.y] * weights.y +
               bones[in.boneIndices.z] * weights.z +
               bones[in.boneIndices.w] * weights.w;
        skinPrev = prevBones[in.boneIndices.x] * weights.x +
                   prevBones[in.boneIndices.y] * weights.y +
                   prevBones[in.boneIndices.z] * weights.z +
                   prevBones[in.boneIndices.w] * weights.w;
    }

    float4 skinnedPos = skin * float4(in.position, 1.0);
    float4 prevSkinnedPos = skinPrev * float4(in.position, 1.0);
    float4 worldPos = model.modelMatrix * skinnedPos;
    float4 prevWorldPos = velocity.prevModelMatrix * prevSkinnedPos;
    float weight = saturate(in.color.a);
    worldPos.xyz = applyWindOffset(worldPos.xyz, weight, material, camera);
    prevWorldPos.xyz = applyWindOffset(prevWorldPos.xyz, weight, material, camera);

    float4 currClip = velocity.currViewProjection * worldPos;
    float4 prevClip = velocity.prevViewProjection * prevWorldPos;
    out.position = currClip;
    out.currClip = currClip;
    out.prevClip = prevClip;
    return out;
}

vertex VertexOut vertex_main(
    VertexIn in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    constant MaterialUniforms& material [[buffer(3)]],
    constant MeshUniforms& mesh [[buffer(4)]]
) {
    VertexOut out;
    float3 boundsCenter = mesh.boundsCenter.xyz;
    float3 boundsSize = mesh.boundsSize.xyz;
    float3 centerWS = (model.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float dist = distance(camera.cameraPositionTime.xyz, centerWS);
    float lodFade = (material.foliageParams2.y > 0.5) ? computeFade(dist, material.foliageParams1.x, material.foliageParams1.y) : 0.0;
    float billboardFade = (material.foliageParams2.z > 0.5) ? computeFade(dist, material.foliageParams1.z, material.foliageParams1.w) : 0.0;

    float weight = saturate(in.color.a);
    float3 worldNormal = float3(0.0, 1.0, 0.0);
    float3 worldPos = float3(0.0);
    float3 tangent = float3(1.0, 0.0, 0.0);
    float3 bitangent = float3(0.0, 0.0, 1.0);
    float3 toCamDir = float3(0.0, 0.0, 1.0);
    if (mesh.flags.x > 0.5 && material.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffset(centerWS, weight, material, camera);
        float3 toCam = normalize(camera.cameraPositionTime.xyz - windCenter);
        toCamDir = toCam;
        float3 upRef = (abs(toCam.y) < 0.98) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        float3 right = normalize(cross(upRef, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = model.modelMatrix[0].xyz;
        float3 axisY = model.modelMatrix[1].xyz;
        float3 axisZ = model.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
        worldNormal = normalize(toCam);
        tangent = right;
        bitangent = billUp;
    } else {
        float4 wp = model.modelMatrix * float4(in.position, 1.0);
        wp.xyz = applyWindOffset(wp.xyz, weight, material, camera);
        worldPos = wp.xyz;
        worldNormal = normalize((model.normalMatrix * float4(in.normal, 0.0)).xyz);
        tangent = normalize((model.normalMatrix * float4(in.tangent, 0.0)).xyz);
        bitangent = normalize((model.normalMatrix * float4(in.bitangent, 0.0)).xyz);
    }

    out.worldPosition = worldPos;
    out.position = camera.viewProjectionMatrix * float4(worldPos, 1.0);
    out.normal = worldNormal;
    out.tangent = tangent;
    out.bitangent = bitangent;
    float2 texCoord = in.texCoord;
    if (mesh.flags.x > 0.5 && material.foliageParams2.z > 0.5 && material.impostorParams0.x > 0.5) {
        float3 axisX = normalize(model.modelMatrix[0].xyz);
        float3 axisY = normalize(model.modelMatrix[1].xyz);
        float3 axisZ = normalize(model.modelMatrix[2].xyz);
        float3x3 model3 = float3x3(axisX, axisY, axisZ);
        float3x3 worldToLocal = transpose(model3);
        texCoord = impostorAtlasUV(texCoord, toCamDir, worldToLocal, material);
    }
    out.texCoord = texCoord;
    out.lightmapTexCoord = in.texCoord1 * mesh.lightmapScaleOffset.xy + mesh.lightmapScaleOffset.zw;
    out.color = in.color;
    out.lodFade = lodFade;
    out.billboardFade = billboardFade;
    out.billboardFlag = mesh.flags.x;
    out.bakedLightingFlag = mesh.flags.y;
    out.staticLightmapFlag = mesh.flags.z;
    out.hdrStaticLightmapFlag = mesh.flags.w;

    return out;
}

vertex VertexOut vertex_main_instanced(
    VertexIn in [[stage_in]],
    const device InstanceData* instances [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    constant MaterialUniforms& material [[buffer(3)]],
    constant MeshUniforms& mesh [[buffer(4)]],
    uint instanceId [[instance_id]]
) {
    VertexOut out;
    InstanceData inst = instances[instanceId];
    float3 boundsCenter = mesh.boundsCenter.xyz;
    float3 boundsSize = mesh.boundsSize.xyz;
    float3 centerWS = (inst.modelMatrix * float4(boundsCenter, 1.0)).xyz;
    float dist = distance(camera.cameraPositionTime.xyz, centerWS);
    float lodFade = (material.foliageParams2.y > 0.5) ? computeFade(dist, material.foliageParams1.x, material.foliageParams1.y) : 0.0;
    float billboardFade = (material.foliageParams2.z > 0.5) ? computeFade(dist, material.foliageParams1.z, material.foliageParams1.w) : 0.0;

    float weight = saturate(in.color.a);
    float3 worldNormal = float3(0.0, 1.0, 0.0);
    float3 worldPos = float3(0.0);
    float3 tangent = float3(1.0, 0.0, 0.0);
    float3 bitangent = float3(0.0, 0.0, 1.0);
    float3 toCamDir = float3(0.0, 0.0, 1.0);
    if (mesh.flags.x > 0.5 && material.foliageParams2.z > 0.5) {
        float3 windCenter = applyWindOffset(centerWS, weight, material, camera);
        float3 toCam = normalize(camera.cameraPositionTime.xyz - windCenter);
        toCamDir = toCam;
        float3 upRef = (abs(toCam.y) < 0.98) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        float3 right = normalize(cross(upRef, toCam));
        float3 billUp = normalize(cross(toCam, right));
        float3 axisX = inst.modelMatrix[0].xyz;
        float3 axisY = inst.modelMatrix[1].xyz;
        float3 axisZ = inst.modelMatrix[2].xyz;
        float uniformScale = max(length(axisX), max(length(axisY), length(axisZ)));
        float width = max(boundsSize.x, boundsSize.z) * uniformScale;
        float height = max(boundsSize.y, 0.0001) * uniformScale;
        float2 quad = float2(in.position.x, in.position.z);
        worldPos = windCenter + right * (quad.x * width) + billUp * (quad.y * height);
        worldNormal = normalize(toCam);
        tangent = right;
        bitangent = billUp;
    } else {
        float4 wp = inst.modelMatrix * float4(in.position, 1.0);
        wp.xyz = applyWindOffset(wp.xyz, weight, material, camera);
        worldPos = wp.xyz;
        worldNormal = normalize((inst.normalMatrix * float4(in.normal, 0.0)).xyz);
        tangent = normalize((inst.normalMatrix * float4(in.tangent, 0.0)).xyz);
        bitangent = normalize((inst.normalMatrix * float4(in.bitangent, 0.0)).xyz);
    }

    out.worldPosition = worldPos;
    out.position = camera.viewProjectionMatrix * float4(worldPos, 1.0);
    out.normal = worldNormal;
    out.tangent = tangent;
    out.bitangent = bitangent;
    float2 texCoord = in.texCoord;
    if (mesh.flags.x > 0.5 && material.foliageParams2.z > 0.5 && material.impostorParams0.x > 0.5) {
        float3 axisX = normalize(inst.modelMatrix[0].xyz);
        float3 axisY = normalize(inst.modelMatrix[1].xyz);
        float3 axisZ = normalize(inst.modelMatrix[2].xyz);
        float3x3 model3 = float3x3(axisX, axisY, axisZ);
        float3x3 worldToLocal = transpose(model3);
        texCoord = impostorAtlasUV(texCoord, toCamDir, worldToLocal, material);
    }
    out.texCoord = texCoord;
    out.lightmapTexCoord = in.texCoord1 * mesh.lightmapScaleOffset.xy + mesh.lightmapScaleOffset.zw;
    out.color = in.color;
    out.lodFade = lodFade;
    out.billboardFade = billboardFade;
    out.billboardFlag = mesh.flags.x;
    out.bakedLightingFlag = mesh.flags.y;
    out.staticLightmapFlag = mesh.flags.z;
    out.hdrStaticLightmapFlag = mesh.flags.w;
    return out;
}

vertex VertexOut vertex_skinned(
    VertexInSkinned in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    const device float4x4* bones [[buffer(3)]]
) {
    VertexOut out;

    float4x4 skin = float4x4(1.0);
    float totalWeight = in.boneWeights.x + in.boneWeights.y + in.boneWeights.z + in.boneWeights.w;
    if (totalWeight > 0.0) {
        float4 weights = in.boneWeights / totalWeight;
        skin = bones[in.boneIndices.x] * weights.x +
               bones[in.boneIndices.y] * weights.y +
               bones[in.boneIndices.z] * weights.z +
               bones[in.boneIndices.w] * weights.w;
    }

    float4 skinnedPos = skin * float4(in.position, 1.0);
    float4 worldPos = model.modelMatrix * skinnedPos;
    out.worldPosition = worldPos.xyz;
    out.position = camera.viewProjectionMatrix * worldPos;

    float3x3 skin3 = float3x3(skin[0].xyz, skin[1].xyz, skin[2].xyz);

    out.normal = normalize((model.normalMatrix * float4(skin3 * in.normal, 0.0)).xyz);
    out.tangent = normalize((model.normalMatrix * float4(skin3 * in.tangent, 0.0)).xyz);
    out.bitangent = normalize((model.normalMatrix * float4(skin3 * in.bitangent, 0.0)).xyz);

    out.texCoord = in.texCoord;
    out.lightmapTexCoord = in.texCoord1;
    out.color = in.color;
    out.lodFade = 0.0;
    out.billboardFade = 0.0;
    out.billboardFlag = 0.0;
    out.bakedLightingFlag = 0.0;
    out.staticLightmapFlag = 0.0;
    out.hdrStaticLightmapFlag = 0.0;

    return out;
}

// ============================================================================
// FRAGMENT SHADER
// ============================================================================

fragment float4 fragment_prepass(
    PrepassOut in [[stage_in]],
    constant MaterialUniforms& material [[buffer(0)]],
    texture2d<float> roughnessMap [[texture(0)]],
    texture2d<float> ormMap [[texture(1)]],
    texture2d<float> albedoMap [[texture(2)]],
    texture2d<float> terrainControlMap [[texture(3)]],
    texture2d<float> terrainLayer0Map [[texture(4)]],
    texture2d<float> terrainLayer1Map [[texture(5)]],
    texture2d<float> terrainLayer2Map [[texture(6)]],
    texture2d<float> terrainLayer0OrmMap [[texture(7)]],
    texture2d<float> terrainLayer1OrmMap [[texture(8)]],
    texture2d<float> terrainLayer2OrmMap [[texture(9)]],
    sampler textureSampler [[sampler(0)]]
) {
    float3 n = normalize(in.normalVS);
    float2 controlUV = clamp(in.texCoord, float2(0.0), float2(1.0));
    float2 uv = in.texCoord * material.uvTilingOffset.xy + material.uvTilingOffset.zw;
    bool terrainEnabled = material.terrainParams0.x > 0.5 &&
        (material.terrainFlags.y + material.terrainFlags.z + material.terrainFlags.w) > 0.5;
    float alpha = material.albedo.a;
    if (terrainEnabled) {
        float2 uv0 = uv * max(material.terrainLayer0ST.xy, float2(0.001));
        float2 uv1 = uv * max(material.terrainLayer1ST.xy, float2(0.001));
        float2 uv2 = uv * max(material.terrainLayer2ST.xy, float2(0.001));
        float3 weights = float3(1.0, 0.0, 0.0);
        if (material.terrainFlags.x > 0.5) {
            weights = normalizedTerrainWeights(terrainControlMap.sample(textureSampler, controlUV).rgb, material.terrainParams0.y);
        }
        float a0 = terrainLayer0Map.sample(textureSampler, uv0).a;
        float a1 = terrainLayer1Map.sample(textureSampler, uv1).a;
        float a2 = terrainLayer2Map.sample(textureSampler, uv2).a;
        alpha *= dot(weights, float3(a0, a1, a2));
    } else if (material.textureFlags.x > 0.5) {
        alpha *= albedoMap.sample(textureSampler, uv).a;
    }
    if (material.textureFlags3.y > 0.5 && alpha < material.textureFlags3.z) {
        discard_fragment();
    }
    if (material.foliageParams2.w > 0.5) {
        float fade = 1.0;
        if (material.foliageParams2.z > 0.5) {
            fade *= (in.billboardFlag > 0.5) ? in.billboardFade : (1.0 - in.billboardFade);
        }
        if (material.foliageParams2.y > 0.5) {
            fade *= (1.0 - in.lodFade);
        }
        if (fade < 0.999) {
            float2 p = floor(in.position.xy);
            float noise = fract(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
            if (noise > fade) {
                discard_fragment();
            }
        }
    }
    float roughness = clamp(material.properties.y, 0.04, 1.0);
    if (terrainEnabled) {
        float2 uv0 = uv * max(material.terrainLayer0ST.xy, float2(0.001));
        float2 uv1 = uv * max(material.terrainLayer1ST.xy, float2(0.001));
        float2 uv2 = uv * max(material.terrainLayer2ST.xy, float2(0.001));
        float3 weights = float3(1.0, 0.0, 0.0);
        if (material.terrainFlags.x > 0.5) {
            weights = normalizedTerrainWeights(terrainControlMap.sample(textureSampler, controlUV).rgb, material.terrainParams0.y);
        }
        float ormRough = terrainLayer0OrmMap.sample(textureSampler, uv0).g * weights.x
            + terrainLayer1OrmMap.sample(textureSampler, uv1).g * weights.y
            + terrainLayer2OrmMap.sample(textureSampler, uv2).g * weights.z;
        roughness = clamp(roughness * ormRough, 0.04, 1.0);
    } else if (material.textureFlags3.x > 0.5) {
        float3 orm = ormMap.sample(textureSampler, uv).rgb;
        if (material.textureFlags.w > 0.5) {
            roughness = clamp(roughness * roughnessMap.sample(textureSampler, uv).r, 0.04, 1.0);
        } else {
            roughness = clamp(roughness * orm.g, 0.04, 1.0);
        }
    } else if (material.textureFlags.w > 0.5) {
        roughness = clamp(roughness * roughnessMap.sample(textureSampler, uv).r, 0.04, 1.0);
    }
    return float4(n * 0.5 + 0.5, roughness);
}

fragment float4 fragment_velocity(
    VelocityOut in [[stage_in]]
) {
    if (in.currClip.w <= 0.0001 || in.prevClip.w <= 0.0001) {
        return float4(0.0);
    }

    float2 currNdc = in.currClip.xy / in.currClip.w;
    float2 prevNdc = in.prevClip.xy / in.prevClip.w;
    float2 currUV = float2(currNdc.x * 0.5 + 0.5,
                           1.0 - (currNdc.y * 0.5 + 0.5));
    float2 prevUV = float2(prevNdc.x * 0.5 + 0.5,
                           1.0 - (prevNdc.y * 0.5 + 0.5));
    float2 velocity = currUV - prevUV;
    return float4(velocity, 0.0, 1.0);
}

// ========================================================================
// INSTANCE GPU CULLING
// ========================================================================

kernel void hzb_init(depth2d<float, access::sample> depthTex [[texture(0)]],
                     texture2d<float, access::write> hzbTex [[texture(1)]],
                     sampler depthSampler [[sampler(0)]],
                     uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= hzbTex.get_width() || gid.y >= hzbTex.get_height()) {
        return;
    }

    float2 texSize = float2(hzbTex.get_width(), hzbTex.get_height());
    float2 uv = (float2(gid) + 0.5) / texSize;
    float depth = depthTex.sample(depthSampler, uv);
    hzbTex.write(depth, gid);
}

kernel void hzb_downsample(texture2d<float, access::read> srcTex [[texture(0)]],
                           texture2d<float, access::write> dstTex [[texture(1)]],
                           uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= dstTex.get_width() || gid.y >= dstTex.get_height()) {
        return;
    }

    uint2 base = gid * 2;
    uint srcWidth = srcTex.get_width();
    uint srcHeight = srcTex.get_height();
    uint2 p0 = uint2(min(base.x, srcWidth - 1), min(base.y, srcHeight - 1));
    uint2 p1 = uint2(min(base.x + 1, srcWidth - 1), min(base.y, srcHeight - 1));
    uint2 p2 = uint2(min(base.x, srcWidth - 1), min(base.y + 1, srcHeight - 1));
    uint2 p3 = uint2(min(base.x + 1, srcWidth - 1), min(base.y + 1, srcHeight - 1));

    float d0 = srcTex.read(p0).r;
    float d1 = srcTex.read(p1).r;
    float d2 = srcTex.read(p2).r;
    float d3 = srcTex.read(p3).r;
    float maxDepth = max(max(d0, d1), max(d2, d3));
    dstTex.write(maxDepth, gid);
}

kernel void instance_cull(const device InstanceData* inInstances [[buffer(0)]],
                          device InstanceData* outInstances [[buffer(1)]],
                          device atomic_uint* counters [[buffer(2)]],
                          constant InstanceCullParams& params [[buffer(3)]],
                          uint tid [[thread_position_in_grid]]) {
    if (tid >= params.instanceCount) {
        return;
    }

    InstanceData inst = inInstances[params.inputOffset + tid];
    float3 worldCenter = (inst.modelMatrix * float4(params.boundsCenterRadius.xyz, 1.0)).xyz;

    float3 axisX = inst.modelMatrix[0].xyz;
    float3 axisY = inst.modelMatrix[1].xyz;
    float3 axisZ = inst.modelMatrix[2].xyz;
    float maxScale = max(length(axisX), max(length(axisY), length(axisZ)));
    float radius = params.boundsCenterRadius.w * maxScale;

    for (uint i = 0; i < 6; ++i) {
        float4 p = params.frustumPlanes[i];
        float d = dot(p, float4(worldCenter, 1.0));
        if (d < -radius) {
            return;
        }
    }

    uint idx = atomic_fetch_add_explicit(&counters[0], 1, memory_order_relaxed);
    outInstances[params.outputOffset + idx] = inst;
}

kernel void instance_cull_hzb(const device InstanceData* inInstances [[buffer(0)]],
                              device InstanceData* outInstances [[buffer(1)]],
                              device atomic_uint* counters [[buffer(2)]],
                              constant InstanceCullParams& params [[buffer(3)]],
                              constant CameraUniforms& camera [[buffer(4)]],
                              texture2d<float, access::read> hzbTex [[texture(0)]],
                              uint tid [[thread_position_in_grid]]) {
    if (tid >= params.instanceCount) {
        return;
    }

    InstanceData inst = inInstances[params.inputOffset + tid];
    float3 worldCenter = (inst.modelMatrix * float4(params.boundsCenterRadius.xyz, 1.0)).xyz;

    float3 axisX = inst.modelMatrix[0].xyz;
    float3 axisY = inst.modelMatrix[1].xyz;
    float3 axisZ = inst.modelMatrix[2].xyz;
    float maxScale = max(length(axisX), max(length(axisY), length(axisZ)));
    float radius = params.boundsCenterRadius.w * maxScale;

    for (uint i = 0; i < 6; ++i) {
        float4 p = params.frustumPlanes[i];
        float d = dot(p, float4(worldCenter, 1.0));
        if (d < -radius) {
            return;
        }
    }

    float4 clipCenter = camera.viewProjectionMatrix * float4(worldCenter, 1.0);
    if (clipCenter.w <= 0.0001) {
        return;
    }

    float3 ndc = clipCenter.xyz / clipCenter.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        uint idx = atomic_fetch_add_explicit(&counters[0], 1, memory_order_relaxed);
        outInstances[params.outputOffset + idx] = inst;
        return;
    }

    float3 viewPos = (camera.viewMatrix * float4(worldCenter, 1.0)).xyz;
    float viewZ = max(-viewPos.z, 0.0001);
    float3 nearPos = float3(viewPos.x, viewPos.y, viewPos.z + radius);
    float4 clipNear = camera.projectionMatrix * float4(nearPos, 1.0);
    float depthNear = clipNear.z / max(clipNear.w, 0.0001);

    float2 projScale = float2(camera.projectionMatrix[0][0], camera.projectionMatrix[1][1]);
    float2 ndcRadius = abs((radius / viewZ) * projScale);
    float2 uvRadius = ndcRadius * 0.5 + float2(1.5) / max(params.screenSize, float2(1.0));
    float2 uvMin = clamp(uv - uvRadius, float2(0.0), float2(1.0));
    float2 uvMax = clamp(uv + uvRadius, float2(0.0), float2(1.0));
    float2 rectPixels = max((uvMax - uvMin) * params.screenSize, float2(1.0));
    float rectExtent = max(rectPixels.x, rectPixels.y);
    float lod = clamp(floor(log2(max(rectExtent, 1.0))), 0.0, float(max(params.hzbMipCount, 1u) - 1));
    uint mipLevel = uint(lod);

    uint mipWidth = max(1u, hzbTex.get_width(mipLevel));
    uint mipHeight = max(1u, hzbTex.get_height(mipLevel));
    float2 texelScale = float2(mipWidth, mipHeight);
    uint2 texelMin = uint2(clamp(uvMin * texelScale, float2(0.0), float2(mipWidth - 1, mipHeight - 1)));
    uint2 texelMax = uint2(clamp(uvMax * texelScale, float2(0.0), float2(mipWidth - 1, mipHeight - 1)));
    uint2 texelCenter = uint2(clamp(uv * texelScale, float2(0.0), float2(mipWidth - 1, mipHeight - 1)));
    float hzbDepth = hzbTex.read(texelCenter, mipLevel).r;
    hzbDepth = max(hzbDepth, hzbTex.read(uint2(texelMin.x, texelMin.y), mipLevel).r);
    hzbDepth = max(hzbDepth, hzbTex.read(uint2(texelMax.x, texelMin.y), mipLevel).r);
    hzbDepth = max(hzbDepth, hzbTex.read(uint2(texelMin.x, texelMax.y), mipLevel).r);
    hzbDepth = max(hzbDepth, hzbTex.read(uint2(texelMax.x, texelMax.y), mipLevel).r);

    float kDepthBias = 0.0015 + float(mipLevel) * 0.00075;
    if (depthNear > hzbDepth + kDepthBias) {
        return;
    }

    uint idx = atomic_fetch_add_explicit(&counters[0], 1, memory_order_relaxed);
    outInstances[params.outputOffset + idx] = inst;
}

kernel void instance_build_indirect(const device atomic_uint* counters [[buffer(0)]],
                                    device DrawIndexedIndirectArgs* args [[buffer(1)]],
                                    uint gid [[thread_position_in_grid]]) {
    uint count = atomic_load_explicit(&counters[gid], memory_order_relaxed);
    args[gid].instanceCount = count;
}

fragment float4 ssao_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant SSAOParams& params [[buffer(1)]],
    depth2d<float> depthTex [[texture(0)]],
    texture2d<float> normalTex [[texture(1)]],
    texture2d<float> noiseTex [[texture(2)]],
    sampler ssaoSampler [[sampler(0)]]
) {
    float depth = depthTex.sample(ssaoSampler, in.uv);
    if (depth >= 1.0) {
        return float4(1.0);
    }

    float3 viewPos = reconstructViewPosition(in.uv, depth, camera);
    float3 normalVS = normalize(normalTex.sample(ssaoSampler, in.uv).xyz * 2.0 - 1.0);
    float3 worldPos = (camera.viewMatrixInverse * float4(viewPos, 1.0)).xyz;
    float2 worldCell = floor(worldPos.xz / max(params.settings.x * 2.5, 0.2));
    float noiseSeed = hash21(worldCell + worldPos.y * 0.173);
    float angle = noiseSeed * TWO_PI;

    float3 upRef = (abs(normalVS.y) < 0.98) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangentBase = normalize(cross(upRef, normalVS));
    float3 bitangentBase = cross(normalVS, tangentBase);
    float sinA = sin(angle);
    float cosA = cos(angle);
    float3 tangent = tangentBase * cosA + bitangentBase * sinA;
    float3 bitangent = cross(normalVS, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normalVS);

    float occlusion = 0.0;
    float distanceFade = clamp(abs(viewPos.z) * 0.035, 0.65, 1.0);
    float radius = params.settings.x * distanceFade;
    float bias = params.settings.y;

    (void)noiseTex;
    (void)params.noiseScale;

    for (int i = 0; i < 16; ++i) {
        float scale = float(i) / 16.0;
        float3 sampleVec = TBN * kSSAOKernel[i];
        scale = mix(0.1, 1.0, scale * scale);
        float3 samplePos = viewPos + sampleVec * radius * scale;

        float4 offset = camera.projectionMatrix * float4(samplePos, 1.0);
        float2 ndc = offset.xy / max(offset.w, 0.0001);
        float2 sampleUV = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        float sampleDepth = depthTex.sample(ssaoSampler, sampleUV);
        if (sampleDepth >= 1.0) {
            continue;
        }

        float3 sampleView = reconstructViewPosition(sampleUV, sampleDepth, camera);
        float range = smoothstep(0.0, 1.0, radius / max(abs(viewPos.z - sampleView.z), 0.0001));
        float occluded = sampleView.z >= (samplePos.z + bias) ? 1.0 : 0.0;
        occlusion += occluded * range;
    }

    occlusion = 1.0 - (occlusion / 16.0);
    occlusion = pow(occlusion, params.settings.w);
    occlusion = mix(1.0, occlusion, params.settings.z);
    return float4(occlusion, occlusion, occlusion, 1.0);
}

fragment float4 ssao_blur_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant SSAOBlurParams& params [[buffer(1)]],
    texture2d<float> ssaoTex [[texture(0)]],
    depth2d<float> depthTex [[texture(1)]],
    texture2d<float> normalTex [[texture(2)]],
    sampler ssaoSampler [[sampler(0)]]
) {
    float centerDepth = depthTex.sample(ssaoSampler, in.uv);
    if (centerDepth >= 1.0) {
        return float4(1.0);
    }
    float centerLinear = linearizeDepth(centerDepth, camera);
    float3 centerNormal = normalize(normalTex.sample(ssaoSampler, in.uv).xyz * 2.0 - 1.0);

    float2 offsets[9] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0), float2(-1.0, 0.0),
        float2(0.0, 1.0), float2(0.0, -1.0),
        float2(1.0, 1.0), float2(-1.0, 1.0),
        float2(1.0, -1.0), float2(-1.0, -1.0)
    };

    float sum = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < 9; ++i) {
        float2 uv = in.uv + offsets[i] * params.texelSize;
        float sampleDepth = depthTex.sample(ssaoSampler, uv);
        if (sampleDepth >= 1.0) {
            continue;
        }
        float sampleLinear = linearizeDepth(sampleDepth, camera);
        float depthWeight = exp(-abs(sampleLinear - centerLinear) * params.depthSharpness);
        float3 sampleNormal = normalize(normalTex.sample(ssaoSampler, uv).xyz * 2.0 - 1.0);
        float normalWeight = pow(saturate(dot(sampleNormal, centerNormal)), params.normalSharpness);
        float weight = depthWeight * normalWeight;
        float sample = ssaoTex.sample(ssaoSampler, uv).r;
        sum += sample * weight;
        weightSum += weight;
    }

    float result = (weightSum > 0.0) ? (sum / weightSum) : 1.0;
    return float4(result, result, result, 1.0);
}

fragment float4 bloom_prefilter_fragment(
    BlitVertexOut in [[stage_in]],
    constant BloomPrefilterParams& params [[buffer(0)]],
    texture2d<float> source [[texture(0)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float3 color = source.sample(sourceSampler, in.uv).rgb;
    float brightness = max(max(color.r, color.g), color.b);
    float threshold = params.threshold;
    float knee = params.knee;
    float contrib = max(brightness - threshold, 0.0);
    if (knee > 0.0) {
        float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
        soft = (soft * soft) / max(4.0 * knee + 1e-5, 1e-5);
        contrib = max(contrib, soft);
    }
    float scale = (brightness > 1e-5) ? (contrib / brightness) : 0.0;
    return float4(color * scale, 1.0);
}

fragment float4 bloom_blur_fragment(
    BlitVertexOut in [[stage_in]],
    constant BloomBlurParams& params [[buffer(0)]],
    texture2d<float> source [[texture(0)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float2 dir = params.direction * params.texelSize;
    float3 color = source.sample(sourceSampler, in.uv).rgb * 0.227027;
    color += source.sample(sourceSampler, in.uv + dir * 1.384615).rgb * 0.316216;
    color += source.sample(sourceSampler, in.uv - dir * 1.384615).rgb * 0.316216;
    color += source.sample(sourceSampler, in.uv + dir * 3.230769).rgb * 0.070270;
    color += source.sample(sourceSampler, in.uv - dir * 3.230769).rgb * 0.070270;
    return float4(color, 1.0);
}

fragment float4 bloom_downsample_fragment(
    BlitVertexOut in [[stage_in]],
    constant BloomDownsampleParams& params [[buffer(0)]],
    texture2d<float> source [[texture(0)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float2 texel = params.texelSize;
    float2 offset = texel * 0.5;
    float3 color = source.sample(sourceSampler, in.uv + float2(-offset.x, -offset.y)).rgb;
    color += source.sample(sourceSampler, in.uv + float2(offset.x, -offset.y)).rgb;
    color += source.sample(sourceSampler, in.uv + float2(-offset.x, offset.y)).rgb;
    color += source.sample(sourceSampler, in.uv + float2(offset.x, offset.y)).rgb;
    color *= 0.25;
    return float4(color, 1.0);
}

fragment float4 bloom_upsample_fragment(
    BlitVertexOut in [[stage_in]],
    constant BloomUpsampleParams& params [[buffer(0)]],
    texture2d<float> source [[texture(0)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float2 texel = params.texelSize * params.radius;
    float3 color = source.sample(sourceSampler, in.uv).rgb * 4.0;
    color += source.sample(sourceSampler, in.uv + float2(texel.x, 0.0)).rgb * 2.0;
    color += source.sample(sourceSampler, in.uv + float2(-texel.x, 0.0)).rgb * 2.0;
    color += source.sample(sourceSampler, in.uv + float2(0.0, texel.y)).rgb * 2.0;
    color += source.sample(sourceSampler, in.uv + float2(0.0, -texel.y)).rgb * 2.0;
    color += source.sample(sourceSampler, in.uv + float2(texel.x, texel.y)).rgb;
    color += source.sample(sourceSampler, in.uv + float2(-texel.x, texel.y)).rgb;
    color += source.sample(sourceSampler, in.uv + float2(texel.x, -texel.y)).rgb;
    color += source.sample(sourceSampler, in.uv + float2(-texel.x, -texel.y)).rgb;
    color *= 1.0 / 16.0;
    return float4(color, 1.0);
}

fragment float4 bloom_combine_fragment(
    BlitVertexOut in [[stage_in]],
    constant BloomCombineParams& params [[buffer(0)]],
    constant PostProcessParams& postParams [[buffer(1)]],
    texture2d<float> sceneTex [[texture(0)]],
    texture2d<float> bloomTex [[texture(1)]],
    texture2d<float> lutTex [[texture(2)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float3 sceneColor = sceneTex.sample(sourceSampler, in.uv).rgb;
    float3 bloomColor = bloomTex.sample(sourceSampler, in.uv).rgb * params.intensity;
    float3 color = sceneColor + bloomColor;
    float toneMapping = postParams.params1.y;
    float inputIsLinear = postParams.params1.z;
    if (toneMapping > 0.5) {
        if (toneMapping < 1.5) {
            color = tonemapFilmic(color);
        } else {
            color = tonemapACES(color);
        }
    } else if (inputIsLinear > 0.5) {
        // Keep bloom usable even when tone mapping is disabled.
        color = pow(max(color, float3(0.0)), float3(1.0 / 2.2));
    }
    float gradingIntensity = clamp(postParams.params1.x, 0.0, 1.0);
    if (gradingIntensity > 0.001) {
        float3 graded = sampleLUT2D(color, lutTex, sourceSampler);
        color = mix(color, graded, gradingIntensity);
    }

    float vignetteIntensity = clamp(postParams.params0.x, 0.0, 1.0);
    if (vignetteIntensity > 0.001) {
        float2 uv = in.uv - 0.5;
        float vig = smoothstep(0.4, 0.9, dot(uv, uv));
        color *= mix(1.0, 1.0 - vignetteIntensity, vig);
    }
    float grainIntensity = clamp(postParams.params0.y, 0.0, 1.0);
    if (grainIntensity > 0.0001) {
        float grainScale = max(postParams.params0.z, 1.0);
        float t = postParams.params0.w;
        float2 uv = in.uv * grainScale + float2(t * 0.17, t * 0.29);
        float noise = fract(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
        float grain = (noise - 0.5) * 2.0;
        color = max(color + grain * grainIntensity, 0.0);
    }
    if (grainIntensity < 0.0001) {
        float ditherAmount = (toneMapping > 0.5 || gradingIntensity > 0.001) ? 1.0 : 0.0;
        if (ditherAmount > 0.0) {
            float2 pixel = in.uv * float2(sceneTex.get_width(), sceneTex.get_height());
            color = applyDither(color, pixel, ditherAmount);
        }
    }
    return float4(color, 1.0);
}

fragment float4 ssr_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant SSRParams& params [[buffer(1)]],
    texture2d<float> sceneTex [[texture(0)]],
    depth2d<float> depthTex [[texture(1)]],
    texture2d<float> normalTex [[texture(2)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float depth = depthTex.sample(sourceSampler, in.uv);
    float3 current = sceneTex.sample(sourceSampler, in.uv).rgb;
    if (depth >= 1.0) {
        return float4(current, 1.0);
    }

    float3 viewPos = reconstructViewPosition(in.uv, depth, camera);
    float4 normalSample = normalTex.sample(sourceSampler, in.uv);
    float3 normalVS = normalize(normalSample.xyz * 2.0 - 1.0);
    float roughness = clamp(normalSample.w, 0.0, 1.0);
    float3 viewDir = normalize(viewPos);
    float3 reflDir = reflect(viewDir, normalVS);
    if (reflDir.z > -0.01) {
        return float4(current, 1.0);
    }

    float maxSteps = max(params.settings0.w, 1.0);
    float maxDistance = max(params.settings1.x, 0.1);
    float maxRoughness = params.settings1.y;
    if (maxRoughness <= 0.0 || roughness > maxRoughness) {
        return float4(current, 1.0);
    }
    float thickness = params.settings0.z;
    float stepSize = maxDistance / maxSteps;
    float3 prevPos = viewPos;
    float2 hitUV = float2(-1.0);
    float3 hitView = float3(0.0);
    float hit = 0.0;

    float pixelJitter = hash21(floor(in.uv * float2(sceneTex.get_width(), sceneTex.get_height())));
    for (int i = 0; i < 128; ++i) {
        if (float(i) >= maxSteps) {
            break;
        }
        float3 rayPos = viewPos + reflDir * (stepSize * (float(i) + 0.5 + pixelJitter));
        float4 clip = camera.projectionMatrix * float4(rayPos, 1.0);
        if (clip.w <= 0.0001) {
            break;
        }
        float2 uv = float2(clip.x / clip.w * 0.5 + 0.5, 1.0 - (clip.y / clip.w * 0.5 + 0.5));
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            break;
        }
        float sampleDepth = depthTex.sample(sourceSampler, uv);
        if (sampleDepth < 1.0) {
            float3 sampleView = reconstructViewPosition(uv, sampleDepth, camera);
            float depthDelta = abs(sampleView.z - rayPos.z);
            if (rayPos.z <= sampleView.z + thickness && depthDelta <= max(thickness * 2.0, 0.05)) {
                float3 start = prevPos;
                float3 end = rayPos;
                float2 refineUV = uv;
                float3 refineView = sampleView;
                for (int j = 0; j < 4; ++j) {
                    float3 mid = (start + end) * 0.5;
                    float4 midClip = camera.projectionMatrix * float4(mid, 1.0);
                    if (midClip.w <= 0.0001) {
                        break;
                    }
                    float2 midUV = float2(midClip.x / midClip.w * 0.5 + 0.5,
                                          1.0 - (midClip.y / midClip.w * 0.5 + 0.5));
                    if (midUV.x < 0.0 || midUV.x > 1.0 || midUV.y < 0.0 || midUV.y > 1.0) {
                        break;
                    }
                    float midDepth = depthTex.sample(sourceSampler, midUV);
                    if (midDepth >= 1.0) {
                        start = mid;
                        continue;
                    }
                    float3 midView = reconstructViewPosition(midUV, midDepth, camera);
                    float midDelta = abs(midView.z - mid.z);
                    if (mid.z <= midView.z + thickness && midDelta <= max(thickness * 2.0, 0.05)) {
                        end = mid;
                        refineUV = midUV;
                        refineView = midView;
                    } else {
                        start = mid;
                    }
                }
                float4 hitNormalSample = normalTex.sample(sourceSampler, refineUV);
                float3 hitNormalVS = normalize(hitNormalSample.xyz * 2.0 - 1.0);
                float facing = dot(hitNormalVS, normalize(-reflDir));
                float hitDistance = length(refineView - viewPos);
                float facingFade = smoothstep(0.08, 0.3, facing);
                if (facingFade > 0.0 && hitDistance <= maxDistance) {
                    hitUV = refineUV;
                    hitView = refineView;
                    hit = facingFade;
                    break;
                }
            }
        }
        prevPos = rayPos;
    }

    if (hit < 0.5) {
        return float4(current, 1.0);
    }

    float3 reflection = sceneTex.sample(sourceSampler, hitUV).rgb;
    float fresnel = pow(1.0 - saturate(dot(normalVS, normalize(-viewDir))), 5.0);
    float2 edge = abs(hitUV - 0.5) * 2.0;
    float edgeFade = saturate((0.94 - max(edge.x, edge.y)) / 0.18);
    float roughnessScale = saturate((maxRoughness - roughness) / max(maxRoughness, 0.0001));
    float dist = length(hitView - viewPos);
    float distFade = saturate(1.0 - dist / max(params.settings1.w, 0.0001));
    float grazingFade = saturate((-reflDir.z - 0.05) / 0.2);
    float weight = hit * fresnel * edgeFade * distFade * roughnessScale * grazingFade;

    float3 color = current + reflection * weight;
    return float4(color, 1.0);
}

struct DecalOut {
    float4 albedo [[color(0)]];
    float4 normal [[color(1)]];
    float4 orm [[color(2)]];
};

fragment DecalOut decal_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant DecalParams& decal [[buffer(1)]],
    depth2d<float> depthTex [[texture(0)]],
    texture2d<float> decalTex [[texture(1)]],
    texture2d<float> normalMap [[texture(2)]],
    texture2d<float> ormMap [[texture(3)]],
    texture2d<float> maskMap [[texture(4)]],
    sampler decalSampler [[sampler(0)]]
) {
    DecalOut out;
    out.albedo = float4(0.0);
    out.normal = float4(0.0);
    out.orm = float4(0.0);

    float depth = depthTex.sample(decalSampler, in.uv);
    if (depth >= 1.0) {
        return out;
    }

    float3 viewPos = reconstructViewPosition(in.uv, depth, camera);
    float3 worldPos = (camera.viewMatrixInverse * float4(viewPos, 1.0)).xyz;
    float3 local = (decal.invModel * float4(worldPos, 1.0)).xyz;
    float3 absLocal = abs(local);
    float inside = step(absLocal.x, 0.5) * step(absLocal.y, 0.5) * step(absLocal.z, 0.5);
    if (inside < 0.5) {
        return out;
    }

    float2 uv = local.xy + 0.5;
    uv = uv * decal.uvTilingOffset.xy + decal.uvTilingOffset.zw;

    float4 sampleColor = decalTex.sample(decalSampler, uv);
    float mask = 1.0;
    if (decal.mapFlags.w > 0.5) {
        mask = maskMap.sample(decalSampler, uv).r;
    }
    float softness = clamp(decal.edgeParams.x, 0.0, 0.49);
    float edge = max(absLocal.x, max(absLocal.y, absLocal.z));
    float edgeFade = 1.0 - smoothstep(0.5 - softness, 0.5, edge);
    float opacity = clamp(decal.colorOpacity.w, 0.0, 1.0) * sampleColor.a * mask * edgeFade;
    if (opacity <= 0.0001) {
        return out;
    }

    float3 tint = decal.colorOpacity.xyz;
    float3 color = sampleColor.rgb * tint;
    out.albedo = float4(color, opacity);

    if (decal.mapFlags.y > 0.5) {
        float3 tangentNormal = normalMap.sample(decalSampler, uv).xyz * 2.0 - 1.0;
        float3x3 invModel3 = float3x3(decal.invModel[0].xyz,
                                      decal.invModel[1].xyz,
                                      decal.invModel[2].xyz);
        float3x3 decalNormalMatrix = transpose(invModel3);
        float3 T = normalize(decalNormalMatrix * float3(1.0, 0.0, 0.0));
        float3 B = normalize(decalNormalMatrix * float3(0.0, 1.0, 0.0));
        float3 N = normalize(decalNormalMatrix * float3(0.0, 0.0, 1.0));
        float3 worldNormal = normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);
        out.normal = float4(worldNormal * 0.5 + 0.5, opacity);
    }

    if (decal.mapFlags.z > 0.5) {
        float3 orm = ormMap.sample(decalSampler, uv).rgb;
        out.orm = float4(orm, opacity);
    }

    return out;
}

fragment float4 taa_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant TAAParams& params [[buffer(1)]],
    texture2d<float> currentTex [[texture(0)]],
    texture2d<float> historyTex [[texture(1)]],
    depth2d<float> depthTex [[texture(2)]],
    texture2d<float> velocityTex [[texture(3)]],
    texture2d<float> normalTex [[texture(4)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float3 current = currentTex.sample(sourceSampler, in.uv).rgb;
    if (params.params0.w < 0.5) {
        return float4(current, 1.0);
    }

    float depth = depthTex.sample(sourceSampler, in.uv);
    if (depth >= 1.0) {
        return float4(current, 1.0);
    }

    float2 prevUV = float2(0.0);
    float2 velocity = float2(0.0);
    if (params.params1.y > 0.5) {
        velocity = velocityTex.sample(sourceSampler, in.uv).rg;
        prevUV = in.uv - velocity;
    } else {
        float3 viewPos = reconstructViewPosition(in.uv, depth, camera);
        float4 worldPos = camera.viewMatrixInverse * float4(viewPos, 1.0);
        float4 prevClip = params.prevViewProjection * worldPos;
        if (prevClip.w <= 0.0001) {
            return float4(current, 1.0);
        }
        float2 prevNdc = prevClip.xy / prevClip.w;
        prevUV = float2(prevNdc.x * 0.5 + 0.5, 1.0 - (prevNdc.y * 0.5 + 0.5));
        velocity = in.uv - prevUV;
    }
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        return float4(current, 1.0);
    }

    float3 history = historyTex.sample(sourceSampler, prevUV).rgb;

    float2 texel = max(params.params0.xy, float2(0.000001));
    float3 minC = current;
    float3 maxC = current;
    float3 meanC = current;
    float3 sqMeanC = current * current;
    float2 offsets[8] = {
        float2(1.0, 0.0), float2(-1.0, 0.0),
        float2(0.0, 1.0), float2(0.0, -1.0),
        float2(1.0, 1.0), float2(-1.0, 1.0),
        float2(1.0, -1.0), float2(-1.0, -1.0)
    };
    for (int i = 0; i < 8; ++i) {
        float3 sampleColor = currentTex.sample(sourceSampler, in.uv + offsets[i] * texel).rgb;
        minC = min(minC, sampleColor);
        maxC = max(maxC, sampleColor);
        meanC += sampleColor;
        sqMeanC += sampleColor * sampleColor;
    }
    meanC *= (1.0 / 9.0);
    sqMeanC *= (1.0 / 9.0);
    float feedback = clamp(params.params0.z, 0.02, 0.25);
    float speedPx = length(velocity / texel);
    float motionFactor = saturate(1.0 - speedPx * 0.15);
    float stabilized = mix(feedback, 0.28, motionFactor);
    float prevDepth = depthTex.sample(sourceSampler, prevUV);
    float currentLinear = linearizeDepth(depth, camera);
    float prevLinear = (prevDepth < 1.0) ? linearizeDepth(prevDepth, camera) : currentLinear + 1e6;
    float depthThreshold = max(params.params1.z * max(currentLinear, 1.0), 0.02);
    float depthDiff = abs(prevLinear - currentLinear);
    float depthWeight = 1.0 - smoothstep(0.0, depthThreshold, depthDiff);
    float4 normalNowSample = normalTex.sample(sourceSampler, in.uv);
    float4 normalPrevSample = normalTex.sample(sourceSampler, prevUV);
    float3 normalNow = normalize(normalNowSample.xyz * 2.0 - 1.0);
    float3 normalPrev = normalize(normalPrevSample.xyz * 2.0 - 1.0);
    float normalDot = clamp(dot(normalNow, normalPrev), 0.0, 1.0);
    float normalWeight = smoothstep(params.params1.w, 1.0, normalDot);
    float roughnessNow = clamp(normalNowSample.w, 0.0, 1.0);
    float roughnessPrev = clamp(normalPrevSample.w, 0.0, 1.0);
    float roughnessMax = max(roughnessNow, roughnessPrev);
    float roughnessWeight = smoothstep(0.12, 0.45, min(roughnessNow, roughnessPrev));
    float roughnessDelta = abs(roughnessNow - roughnessPrev);
    float glossyFactor = 1.0 - smoothstep(0.05, 0.35, roughnessMax);
    float specularStabilityEnabled = params.params2.x;
    float specularStabilityStrength = clamp(params.params2.y, 0.0, 2.0);
    float specularStability = 0.0;
    if (specularStabilityEnabled > 0.5) {
        float roughnessConsistency = 1.0 - smoothstep(0.03, 0.18, roughnessDelta);
        specularStability = glossyFactor * motionFactor * depthWeight * normalWeight * roughnessConsistency;
        specularStability *= saturate(specularStabilityStrength);
    }
    float3 sigma = sqrt(max(sqMeanC - meanC * meanC, float3(0.0))) * 1.25;
    float clipScale = mix(1.0, 1.65, specularStability);
    float3 clipMin = max(minC, meanC - sigma * clipScale);
    float3 clipMax = min(maxC, meanC + sigma * clipScale);
    history = clamp(history, clipMin, clipMax);
    float currLuma = luminance709(current);
    float histLuma = luminance709(history);
    float lumaDelta = abs(currLuma - histLuma) / max(max(currLuma, histLuma), 0.2);
    float lumaWeight = 1.0 - smoothstep(0.05, 0.35, lumaDelta);
    float specularLumaWeight = 1.0 - smoothstep(0.02, 0.18, lumaDelta);
    float baseFeedback = stabilized * depthWeight * normalWeight * roughnessWeight * lumaWeight;
    float specularFeedback = (0.18 + 0.22 * motionFactor) * specularStability * specularLumaWeight;
    feedback = max(baseFeedback, specularFeedback);
    feedback = clamp(feedback, 0.0, 0.92);
    float3 color = mix(current, history, feedback);
    float sharpness = clamp(params.params1.x, 0.0, 1.0);
    sharpness *= 1.0 - 0.45 * specularStability;
    color = mix(color, current, sharpness);
    return float4(color, 1.0);
}

fragment float4 motion_blur_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant MotionBlurParams& params [[buffer(1)]],
    texture2d<float> sceneTex [[texture(0)]],
    depth2d<float> depthTex [[texture(1)]],
    texture2d<float> velocityTex [[texture(2)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float3 current = sceneTex.sample(sourceSampler, in.uv).rgb;
    float depth = depthTex.sample(sourceSampler, in.uv);
    if (depth >= 1.0 || params.params0.z <= 0.001) {
        return float4(current, 1.0);
    }

    float2 velocity = velocityTex.sample(sourceSampler, in.uv).rg * params.params0.z;
    float2 texel = max(params.params0.xy, float2(0.000001));
    float2 velPixels = velocity / texel;
    float speed = length(velPixels);
    if (speed < 0.25) {
        return float4(current, 1.0);
    }
    float centerLinear = linearizeDepth(depth, camera);

    const int maxSamples = 16;
    int sampleCount = clamp(int(ceil(speed * 0.5)), 4, maxSamples);
    float3 accum = float3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < maxSamples; ++i) {
        if (i >= sampleCount) {
            break;
        }
        float t = (float(i) / float(sampleCount - 1)) - 0.5;
        float2 uv = in.uv + velocity * t;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            continue;
        }
        float sampleDepth = depthTex.sample(sourceSampler, uv);
        if (sampleDepth >= 1.0) {
            continue;
        }
        float sampleLinear = linearizeDepth(sampleDepth, camera);
        float depthWeight = 1.0 - smoothstep(0.0, max(0.35, centerLinear * 0.08), abs(sampleLinear - centerLinear));
        float w = max(0.0, 1.0 - abs(t) * 2.0) * depthWeight;
        if (w <= 0.0001) {
            continue;
        }
        accum += sceneTex.sample(sourceSampler, uv).rgb * w;
        weightSum += w;
    }
    float3 color = (weightSum > 0.0) ? (accum / weightSum) : current;
    return float4(color, 1.0);
}

fragment float4 dof_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant DofParams& params [[buffer(1)]],
    texture2d<float> sceneTex [[texture(0)]],
    depth2d<float> depthTex [[texture(1)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float3 current = sceneTex.sample(sourceSampler, in.uv).rgb;
    float depth = depthTex.sample(sourceSampler, in.uv);
    if (depth >= 1.0) {
        return float4(current, 1.0);
    }

    float viewDepth = linearizeDepth(depth, camera);
    float focusDistance = max(params.params0.z, 0.01);
    float aperture = max(params.params0.w, 0.1);
    float coc = abs(viewDepth - focusDistance) / focusDistance;
    float apertureScale = clamp(2.8 / aperture, 0.25, 4.0);
    float maxBlur = max(params.params1.x, 1.0);
    float radius = clamp(coc * apertureScale * maxBlur, 0.0, maxBlur);
    if (radius < 0.25) {
        return float4(current, 1.0);
    }

    float2 texel = max(params.params0.xy, float2(0.000001));
    const int maxSamples = 16;
    int sampleCount = clamp(int(ceil(radius * 0.75)), 4, maxSamples);

    float3 accum = float3(0.0);
    float weightSum = 0.0;
    const float golden = 2.399963;
    for (int i = 0; i <= maxSamples; ++i) {
        if (i > sampleCount) {
            break;
        }
        float2 uv = in.uv;
        float t = 0.0;
        if (i > 0) {
            float sampleIndex = float(i - 1);
            t = (sampleIndex + 0.5) / float(sampleCount);
            float r = radius * sqrt(t);
            float theta = sampleIndex * golden;
            float2 offset = float2(cos(theta), sin(theta)) * r;
            uv += offset * texel;
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
                continue;
            }
        }
        float sampleDepth = (i == 0) ? depth : depthTex.sample(sourceSampler, uv);
        if (i > 0 && sampleDepth >= 1.0) {
            continue;
        }
        float sampleViewDepth = (i == 0) ? viewDepth : linearizeDepth(sampleDepth, camera);
        float sampleRadius = radius;
        float cocWeight = 1.0;
        float depthWeight = 1.0;
        float w = 1.0;
        if (i > 0) {
            float sampleCoc = abs(sampleViewDepth - focusDistance) / focusDistance;
            sampleRadius = clamp(sampleCoc * apertureScale * maxBlur, 0.0, maxBlur);
            cocWeight = 1.0 - smoothstep(maxBlur * 0.1, maxBlur, abs(sampleRadius - radius));
            depthWeight = 1.0 - smoothstep(max(0.5, radius * 0.5), max(1.5, radius * 2.0), abs(sampleViewDepth - viewDepth));
            w = (1.0 - t * 0.5) * max(cocWeight, 0.1) * max(depthWeight, 0.05);
        }
        float3 sampleColor = (i == 0) ? current : sceneTex.sample(sourceSampler, uv).rgb;
        accum += sampleColor * w;
        weightSum += w;
    }
    float3 color = accum / weightSum;
    return float4(color, 1.0);
}

kernel void fog_volume_build(
    texture3d<float, access::write> volumeTex [[texture(0)]],
    texture3d<float, access::sample> historyTex [[texture(1)]],
    depth2d<float> shadowAtlas [[texture(2)]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant FogParams& params [[buffer(1)]],
    const device ShadowGPUData* shadowData [[buffer(2)]],
    sampler shadowSampler [[sampler(0)]],
    sampler linearSampler [[sampler(1)]],
    uint3 tid [[thread_position_in_grid]]
) {
    uint width = volumeTex.get_width();
    uint height = volumeTex.get_height();
    uint depth = volumeTex.get_depth();
    if (tid.x >= width || tid.y >= height || tid.z >= depth) {
        return;
    }

    float2 uv = (float2(tid.x, tid.y) + 0.5) / float2(width, height);
    float slice = clamp((float(tid.z) + 0.5) / float(depth), 0.0, 1.0);

    float nearPlane = max(params.volumeParams.x, 0.001);
    float farPlane = max(params.volumeParams.y, nearPlane + 0.001);
    float viewZ = nearPlane * pow(farPlane / nearPlane, slice);

    // Match the same UV->NDC convention used by depth reconstruction so the
    // froxel volume lines up with world space instead of a screen-space overlay.
    float2 ndc = uvToNdc(uv);
    float4 clip = float4(ndc, 1.0, 1.0);
    float4 view = camera.projectionMatrixNoJitterInverse * clip;
    float3 viewDir = normalize(view.xyz / max(view.w, 0.0001));
    float rayDepth = viewZ / max(-viewDir.z, 0.0001);
    float3 viewPos = viewDir * rayDepth;
    float3 worldPos = (camera.viewMatrixInverse * float4(viewPos, 1.0)).xyz;

    float startDist = max(params.distanceParams.x, 0.0);
    float endDist = max(params.distanceParams.y, startDist + 0.001);
    float distanceFadeIn = smoothstep(startDist, startDist + max(1.0, startDist * 0.1 + 2.0), rayDepth);
    float distanceFadeOut = 1.0 - smoothstep(endDist, endDist + max(2.0, endDist * 0.12), rayDepth);
    float distanceMask = clamp(distanceFadeIn * distanceFadeOut, 0.0, 1.0);

    float baseDensity = clamp(params.fogColorDensity.w, 0.0, 1.0) * 0.08;
    float heightFactor = 1.0;
    if (params.misc.x > 0.5) {
        float falloff = max(params.distanceParams.w, 0.0001);
        heightFactor = exp(-falloff * (worldPos.y - params.distanceParams.z));
        heightFactor = clamp(heightFactor, 0.0, 1.0);
    }
    float extinction = baseDensity * heightFactor * distanceMask;
    float albedo = 0.9;
    float scatterStrength = max(params.sunColor.w, 0.0);
    float3 baseScattering = params.fogColorDensity.rgb * extinction * albedo * 0.08;
    float3 sunScattering = float3(0.0);

    float sunIntensity = clamp(params.sunDirIntensity.w, 0.0, 10.0);
    if (sunIntensity > 0.001) {
        float3 viewDirWorld = normalize(camera.cameraPositionTime.xyz - worldPos);
        float3 sunDir = normalize(-params.sunDirIntensity.xyz);
        float cosTheta = clamp(dot(viewDirWorld, sunDir), -1.0, 1.0);
        float g = clamp(params.misc.y, 0.0, 0.9);
        float phase = henyeyGreensteinPhase(cosTheta, g);
        sunScattering += params.sunColor.rgb * sunIntensity * phase * extinction * 0.28 * scatterStrength;
    }

    float shadowFactor = 1.0;
    if (params.shadowParams.z > 0.5 && params.shadowParams.y >= 1.0) {
        int shadowIndex = (int)round(params.shadowParams.x);
        int cascadeCount = (int)round(params.shadowParams.y);
        if (shadowIndex >= 0) {
            float3 viewPosShadow = (camera.viewMatrix * float4(worldPos, 1.0)).xyz;
            float viewDepth = max(-viewPosShadow.z, 0.0);
            float farLimit = shadowData[shadowIndex + cascadeCount - 1].depthRange.y;
            int current = -1;
            if (viewDepth <= farLimit) {
                for (int c = 0; c < cascadeCount; ++c) {
                    ShadowGPUData s = shadowData[shadowIndex + c];
                    if (viewDepth >= s.depthRange.x && viewDepth <= s.depthRange.y) {
                        current = c;
                        break;
                    }
                    if (viewDepth < s.depthRange.x) {
                        current = c;
                        break;
                    }
                }
            }
            if (current >= 0) {
                ShadowGPUData s = shadowData[shadowIndex + current];
                float4 clip = s.viewProj * float4(worldPos, 1.0);
                float3 ndc = clip.xyz / clip.w;
                if (ndc.x >= -1.0 && ndc.x <= 1.0 && ndc.y >= -1.0 && ndc.y <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0) {
                    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
                    float2 tileMin = s.atlasUV.xy;
                    float2 tileMax = s.atlasUV.xy + s.atlasUV.zw;
                    uv = uv * s.atlasUV.zw + s.atlasUV.xy;
                    if (uv.x >= tileMin.x && uv.x <= tileMax.x && uv.y >= tileMin.y && uv.y <= tileMax.y) {
                        float bias = s.params.x * 0.5;
                        float2 worldCell = floor(worldPos.xz / max(s.depthRange.z * 10.0, 0.05));
                        float seed = hash21(worldCell + s.atlasUV.xy * 2048.0 + s.params.w);
                        float randAngle = fract(sin(seed) * 43758.5453) * TWO_PI;
                        float2 rot = float2(cos(randAngle), sin(randAngle));
                        shadowFactor = sampleShadowDepthPCF(shadowAtlas, shadowSampler, uv, ndc.z, bias, tileMin, tileMax, rot, 1.5);
                        shadowFactor = mix(1.0, shadowFactor, clamp(params.shadowParams.w, 0.0, 1.0));
                    }
                }
            }
        }
    }

    sunScattering *= shadowFactor;
    float3 scattering = baseScattering + sunScattering;

    float maxScatter = max(scattering.r, max(scattering.g, scattering.b));
    if (maxScatter > extinction && maxScatter > 0.000001) {
        scattering *= (extinction / maxScatter);
    }

    float4 current = float4(scattering, extinction);
    float historyWeight = clamp(params.volumeParams.w, 0.0, 0.98);
    if (params.misc.z > 0.5 && historyWeight > 0.0) {
        float logDepth = log(farPlane / nearPlane);
        float4 previous = current;
        bool hasHistorySample = false;
        float4 prevClip = params.prevViewProjection * float4(worldPos, 1.0);
        float3 prevViewPos = (params.prevViewMatrix * float4(worldPos, 1.0)).xyz;
        if (prevClip.w > 0.0001 && prevViewPos.z < -nearPlane && logDepth > 0.0001) {
            float2 prevNdc = prevClip.xy / prevClip.w;
            float2 prevUv = float2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5);
            float prevViewZ = max(-prevViewPos.z, nearPlane);
            float prevSlice = log(prevViewZ / nearPlane) / logDepth;
            if (all(prevUv >= float2(0.0)) && all(prevUv <= float2(1.0)) && prevSlice >= 0.0 && prevSlice <= 1.0) {
                previous = historyTex.sample(linearSampler, float3(prevUv, prevSlice));
                hasHistorySample = true;
            }
        }

        if (hasHistorySample) {
        float4 lowClamp = current * float4(0.65, 0.65, 0.65, 0.7);
        float4 highClamp = current * float4(1.35, 1.35, 1.35, 1.3) + float4(0.002);
        previous = clamp(previous, lowClamp, highClamp);

        float currLum = luminance709(current.rgb);
        float prevLum = luminance709(previous.rgb);
        float lumDelta = abs(prevLum - currLum) / max(max(prevLum, currLum), 0.03);
        float extDelta = abs(previous.a - current.a) / max(max(previous.a, current.a), 0.02);
        float historyConfidence = (1.0 - smoothstep(0.08, 0.45, lumDelta))
            * (1.0 - smoothstep(0.05, 0.3, extDelta));
        current = mix(current, previous, historyWeight * historyConfidence);
        }
    }
    volumeTex.write(current, tid);
}

fragment float4 fog_fragment(
    BlitVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant FogParams& params [[buffer(1)]],
    texture2d<float> sceneTex [[texture(0)]],
    depth2d<float> depthTex [[texture(1)]],
    texture3d<float> volumeTex [[texture(2)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float3 current = sceneTex.sample(sourceSampler, in.uv).rgb;
    float depth = depthTex.sample(sourceSampler, in.uv);

    float startDist = max(params.distanceParams.x, 0.0);
    float endDist = max(params.distanceParams.y, startDist + 0.001);
    float nearPlane = max(params.volumeParams.x, 0.001);
    float farPlane = max(params.volumeParams.y, nearPlane + 0.001);
    float sliceCount = max(params.volumeParams.z, 1.0);
    float logDepth = log(farPlane / nearPlane);
    if (logDepth <= 0.0001) {
        return float4(current, 1.0);
    }

    float distance = min(endDist, farPlane);
    if (depth < 1.0) {
        float3 viewPos = reconstructViewPosition(in.uv, depth, camera);
        distance = min(length(viewPos), distance);
    }

    float segmentStart = clamp(startDist, 0.0, distance);
    float segmentEnd = clamp(endDist, segmentStart, distance);
    if (segmentEnd <= segmentStart + 0.0001) {
        return float4(current, 1.0);
    }

    float2 ndc = uvToNdc(in.uv);
    float4 clip = float4(ndc, 1.0, 1.0);
    float4 view = camera.projectionMatrixNoJitterInverse * clip;
    float3 viewDir = normalize(view.xyz / max(view.w, 0.0001));
    float viewZScale = max(-viewDir.z, 0.0001);

    float travel = segmentEnd - segmentStart;
    int steps = int(clamp(sliceCount * 0.6, 16.0, 64.0));
    float stepLength = travel / max(float(steps), 1.0);

    float3 accum = float3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= steps) {
            break;
        }
        float dist = segmentStart + (float(i) + 0.5) * stepLength;
        dist = max(dist, nearPlane);
        float viewZ = max(dist * viewZScale, nearPlane);
        float slice = log(viewZ / nearPlane) / logDepth;
        float w = clamp(slice, 0.0, 1.0);
        float3 uvw = float3(in.uv, w);
        float4 fogSample = volumeTex.sample(sourceSampler, uvw);
        float3 scattering = fogSample.rgb;
        float extinction = max(fogSample.a, 0.000001);
        float stepExt = extinction * stepLength;
        float att = exp(-stepExt);
        float scatterStep = (1.0 - att) / extinction;
        accum += scattering * scatterStep * transmittance;
        transmittance *= att;
    }

    float3 color = current * transmittance + accum;
    return float4(color, 1.0);
}

static inline uint probe_volume_index(uint x, uint y, uint z, uint3 counts) {
    return z * counts.x * counts.y + y * counts.x + x;
}

static inline float3 sample_probe_ambient_cube(const device ProbeAmbientCubeData& probe, float3 normal) {
    float3 n = normalize(normal);
    float3 sq = n * n;
    float3 result = float3(0.0);
    result += (n.x >= 0.0 ? probe.px.rgb : probe.nx.rgb) * sq.x;
    result += (n.y >= 0.0 ? probe.py.rgb : probe.ny.rgb) * sq.y;
    result += (n.z >= 0.0 ? probe.pz.rgb : probe.nz.rgb) * sq.z;
    return max(result, float3(0.0));
}

static inline float sample_probe_visibility_cube(const device ProbeAmbientCubeData& probe, float3 direction) {
    float3 dir = normalize(direction);
    float3 sq = dir * dir;
    float result = 0.0;
    result += (dir.x >= 0.0 ? probe.visibility0.x : probe.visibility0.y) * sq.x;
    result += (dir.y >= 0.0 ? probe.visibility0.z : probe.visibility0.w) * sq.y;
    result += (dir.z >= 0.0 ? probe.visibility1.x : probe.visibility1.y) * sq.z;
    return max(result, 0.0);
}

static inline float3 sample_probe_specular_cube(const device ProbeAmbientCubeData& probe, float3 direction) {
    float3 dir = normalize(direction);
    float3 sq = dir * dir;
    float3 result = float3(0.0);
    result += (dir.x >= 0.0 ? probe.specularPx.rgb : probe.specularNx.rgb) * sq.x;
    result += (dir.y >= 0.0 ? probe.specularPy.rgb : probe.specularNy.rgb) * sq.y;
    result += (dir.z >= 0.0 ? probe.specularPz.rgb : probe.specularNz.rgb) * sq.z;
    return max(result, float3(0.0));
}

static inline float3 sample_probe_specular_filtered_cube(const device ProbeAmbientCubeData& probe,
                                                         float3 direction,
                                                         float3 normal,
                                                         float roughness,
                                                         float filterStrength) {
    float3 dir = normalize(direction);
    float filterAmount = saturate(roughness * max(filterStrength, 0.0));
    if (filterAmount <= 1e-3) {
        return sample_probe_specular_cube(probe, dir);
    }

    float3 up = abs(dir.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, dir));
    float3 bitangent = normalize(cross(dir, tangent));
    float cone = mix(0.08, 0.85, filterAmount * filterAmount);
    float3 centerDir = normalize(mix(dir, normal, filterAmount * 0.25));
    float3 tangentPos = normalize(centerDir + tangent * cone);
    float3 tangentNeg = normalize(centerDir - tangent * cone);
    float3 bitangentPos = normalize(centerDir + bitangent * cone);
    float3 bitangentNeg = normalize(centerDir - bitangent * cone);
    float3 normalDir = normalize(mix(centerDir, normal, 0.6 * filterAmount));

    float centerWeight = mix(1.0, 0.35, filterAmount);
    float edgeWeight = mix(0.0, 0.45, filterAmount);
    float normalWeight = 0.35 * filterAmount;
    float totalWeight = centerWeight + edgeWeight * 4.0 + normalWeight;

    float3 result = sample_probe_specular_cube(probe, centerDir) * centerWeight;
    result += sample_probe_specular_cube(probe, tangentPos) * edgeWeight;
    result += sample_probe_specular_cube(probe, tangentNeg) * edgeWeight;
    result += sample_probe_specular_cube(probe, bitangentPos) * edgeWeight;
    result += sample_probe_specular_cube(probe, bitangentNeg) * edgeWeight;
    result += sample_probe_specular_cube(probe, normalDir) * normalWeight;
    return max(result / max(totalWeight, 1e-4), float3(0.0));
}

static inline float compute_specular_occlusion(float ao, float NdotV, float roughness) {
    float exponent = exp2(-16.0 * roughness - 1.0);
    float visibility = pow(saturate(NdotV + ao), exponent) - 1.0 + ao;
    return saturate(visibility);
}

static inline float compute_probe_directional_occlusion(const device ProbeAmbientCubeData& probe,
                                                        float3 worldPosition,
                                                        float3 sampleDirection,
                                                        float localityRadius) {
    float3 probeToPoint = worldPosition - probe.positionAndValidity.xyz;
    float probeDistance = length(probeToPoint);
    float directionalVisibility = sample_probe_visibility_cube(probe, sampleDirection);
    float referenceDistance = max(probeDistance * 0.35 + localityRadius * 0.65, 0.25);
    float openness = saturate(directionalVisibility / referenceDistance);
    float softened = sqrt(max(openness, 0.0));
    return mix(0.2, 1.0, softened);
}

static inline float3 parallax_correct_probe_direction(const device ProbeAmbientCubeData& probe,
                                                      constant ProbeVolumeUniforms& probeVolume,
                                                      float3 worldPosition,
                                                      float3 reflectionDirection) {
    float3 dir = normalize(reflectionDirection);
    if (probeVolume.featureParams.w < 0.5) {
        return dir;
    }

    float3 boundsMin = probeVolume.boundsMin.xyz;
    float3 boundsMax = probeVolume.boundsMax.xyz;
    float3 clampedPosition = clamp(worldPosition, boundsMin, boundsMax);
    float3 safeDir = select(-max(abs(dir), float3(1e-4)), max(abs(dir), float3(1e-4)), dir >= 0.0);
    float3 targetPlane = select(boundsMin, boundsMax, dir >= 0.0);
    float3 t = (targetPlane - clampedPosition) / safeDir;
    float hitDistance = max(min(t.x, min(t.y, t.z)), 0.0);
    float3 hitPoint = clampedPosition + dir * hitDistance;
    float3 corrected = hitPoint - probe.positionAndValidity.xyz;
    if (length_squared(corrected) <= 1e-6) {
        return dir;
    }
    return normalize(corrected);
}

static inline float3 sample_probe_volume_irradiance(const device ProbeAmbientCubeData* probes,
                                                    constant ProbeVolumeUniforms& probeVolume,
                                                    float3 worldPosition,
                                                    float3 normal) {
    if (probeVolume.gridCounts.w < 0.5) {
        return float3(0.0);
    }

    uint3 counts = uint3(
        max(uint(probeVolume.gridCounts.x + 0.5), 1u),
        max(uint(probeVolume.gridCounts.y + 0.5), 1u),
        max(uint(probeVolume.gridCounts.z + 0.5), 1u)
    );
    float3 extent = max(probeVolume.boundsMax.xyz - probeVolume.boundsMin.xyz, float3(0.001));
    float3 uvw = clamp((worldPosition - probeVolume.boundsMin.xyz) / extent, 0.0, 1.0);
    float3 grid = uvw * float3(
        counts.x > 1 ? float(counts.x - 1) : 1.0,
        counts.y > 1 ? float(counts.y - 1) : 1.0,
        counts.z > 1 ? float(counts.z - 1) : 1.0
    );

    uint3 base = uint3(floor(grid));
    uint3 next = min(base + 1u, counts - 1u);
    float3 frac = fract(grid);

    float3 accum = float3(0.0);
    float accumWeight = 0.0;
    float3 fallbackAccum = float3(0.0);
    float fallbackWeight = 0.0;
    for (uint corner = 0; corner < 8u; ++corner) {
        uint sx = (corner & 1u) != 0u ? next.x : base.x;
        uint sy = (corner & 2u) != 0u ? next.y : base.y;
        uint sz = (corner & 4u) != 0u ? next.z : base.z;
        float wx = (corner & 1u) != 0u ? frac.x : (1.0 - frac.x);
        float wy = (corner & 2u) != 0u ? frac.y : (1.0 - frac.y);
        float wz = (corner & 4u) != 0u ? frac.z : (1.0 - frac.z);
        float baseWeight = wx * wy * wz;
        const device ProbeAmbientCubeData& probe = probes[probe_volume_index(sx, sy, sz, counts)];
        float3 probeIrradiance = sample_probe_ambient_cube(probe, normal);
        fallbackAccum += probeIrradiance * baseWeight;
        fallbackWeight += baseWeight;

        float visibilityWeight = max(probe.positionAndValidity.w, 0.05);
        float3 probeToPoint = worldPosition - probe.positionAndValidity.xyz;
        float probeDistance = length(probeToPoint);
        if (probeDistance > 0.001) {
            float visibilityDistance = sample_probe_visibility_cube(probe, probeToPoint / probeDistance);
            float fadeRange = max(0.3, visibilityDistance * 0.35);
            float leakage = max(probeDistance - visibilityDistance, 0.0);
            float visibilityFade = saturate(1.0 - leakage / fadeRange);
            visibilityWeight *= visibilityFade * visibilityFade;
        }

        float weight = baseWeight * visibilityWeight;
        accum += probeIrradiance * weight;
        accumWeight += weight;
    }

    if (accumWeight > 1e-4) {
        return max(accum / accumWeight, float3(0.0));
    }
    if (fallbackWeight > 1e-4) {
        return max(fallbackAccum / fallbackWeight, float3(0.0));
    }
    return float3(0.0);
}

static inline float4 sample_probe_volume_reflection(const device ProbeAmbientCubeData* probes,
                                                    constant ProbeVolumeUniforms& probeVolume,
                                                    float3 worldPosition,
                                                    float3 reflectionDirection,
                                                    float3 normal,
                                                    float roughness) {
    if (probeVolume.gridCounts.w < 0.5 || probeVolume.featureParams.y < 0.5) {
        return float4(0.0);
    }

    uint3 counts = uint3(
        max(uint(probeVolume.gridCounts.x + 0.5), 1u),
        max(uint(probeVolume.gridCounts.y + 0.5), 1u),
        max(uint(probeVolume.gridCounts.z + 0.5), 1u)
    );
    float3 extent = max(probeVolume.boundsMax.xyz - probeVolume.boundsMin.xyz, float3(0.001));
    float3 uvw = clamp((worldPosition - probeVolume.boundsMin.xyz) / extent, 0.0, 1.0);
    float3 grid = uvw * float3(
        counts.x > 1 ? float(counts.x - 1) : 1.0,
        counts.y > 1 ? float(counts.y - 1) : 1.0,
        counts.z > 1 ? float(counts.z - 1) : 1.0
    );
    float3 cellExtent = extent / float3(
        counts.x > 1 ? float(counts.x - 1) : 1.0,
        counts.y > 1 ? float(counts.y - 1) : 1.0,
        counts.z > 1 ? float(counts.z - 1) : 1.0
    );
    float localityRadius = max(length(cellExtent) * 1.1, 0.35);

    uint3 base = uint3(floor(grid));
    uint3 next = min(base + 1u, counts - 1u);
    float3 frac = fract(grid);
    float roughnessBlend = saturate(roughness * roughness);
    float roughnessFilterStrength = max(probeVolume.reflectionParams.x, 0.0);
    float roughnessFilterAmount = saturate(roughness * roughnessFilterStrength);
    float3 baseSampleDirection = normalize(mix(reflectionDirection, normal, roughnessBlend));
    float candidateWeights[8];
    float3 candidateSamples[8];
    float candidateOcclusion[8];
    float fallbackWeights[8];
    float fallbackOcclusion[8];

    float3 accum = float3(0.0);
    float accumWeight = 0.0;
    float accumOcclusion = 0.0;
    for (uint corner = 0; corner < 8u; ++corner) {
        uint sx = (corner & 1u) != 0u ? next.x : base.x;
        uint sy = (corner & 2u) != 0u ? next.y : base.y;
        uint sz = (corner & 4u) != 0u ? next.z : base.z;
        float wx = (corner & 1u) != 0u ? frac.x : (1.0 - frac.x);
        float wy = (corner & 2u) != 0u ? frac.y : (1.0 - frac.y);
        float wz = (corner & 4u) != 0u ? frac.z : (1.0 - frac.z);
        float baseWeight = wx * wy * wz;

        const device ProbeAmbientCubeData& probe = probes[probe_volume_index(sx, sy, sz, counts)];
        float visibilityWeight = max(probe.positionAndValidity.w, 0.05);
        float3 probeToPoint = worldPosition - probe.positionAndValidity.xyz;
        float probeDistance = length(probeToPoint);
        if (probeDistance > 0.001) {
            float visibilityDistance = sample_probe_visibility_cube(probe, probeToPoint / probeDistance);
            float fadeRange = max(0.3, visibilityDistance * 0.35);
            float leakage = max(probeDistance - visibilityDistance, 0.0);
            float visibilityFade = saturate(1.0 - leakage / fadeRange);
            visibilityWeight *= visibilityFade * visibilityFade;
        }

        float3 correctedDirection = parallax_correct_probe_direction(probe, probeVolume, worldPosition, baseSampleDirection);
        float3 sampleDirection = normalize(mix(correctedDirection, baseSampleDirection, roughnessBlend));
        float3 sampleValue = sample_probe_specular_filtered_cube(
            probe,
            sampleDirection,
            normal,
            roughness,
            roughnessFilterStrength
        );
        float directionalOcclusion = 1.0;
        if (probeVolume.blendParams.z > 0.5) {
            directionalOcclusion = compute_probe_directional_occlusion(probe, worldPosition, sampleDirection, localityRadius);
        }
        float localityWeight = saturate(1.0 - probeDistance / localityRadius);
        localityWeight = max(localityWeight * localityWeight, 0.05);
        localityWeight = mix(localityWeight, sqrt(localityWeight), roughnessFilterAmount);
        candidateSamples[corner] = sampleValue * directionalOcclusion;
        candidateOcclusion[corner] = directionalOcclusion;
        fallbackWeights[corner] = baseWeight * visibilityWeight;
        fallbackOcclusion[corner] = directionalOcclusion;
        candidateWeights[corner] = baseWeight * visibilityWeight * localityWeight;
    }

    float sharpness = mix(max(probeVolume.blendParams.x, 1.0), 1.0, roughnessFilterAmount * 0.85);
    int blendLimit = clamp(int(probeVolume.blendParams.y + 0.5), 1, 8);
    bool consumed[8] = {false, false, false, false, false, false, false, false};
    for (int selection = 0; selection < blendLimit; ++selection) {
        int bestIndex = -1;
        float bestWeight = 0.0;
        for (int candidate = 0; candidate < 8; ++candidate) {
            if (consumed[candidate]) {
                continue;
            }
            if (candidateWeights[candidate] > bestWeight) {
                bestWeight = candidateWeights[candidate];
                bestIndex = candidate;
            }
        }

        if (bestIndex < 0 || bestWeight <= 1e-5) {
            break;
        }

        consumed[bestIndex] = true;
        float selectedWeight = pow(bestWeight, sharpness);
        accum += candidateSamples[bestIndex] * selectedWeight;
        accumWeight += selectedWeight;
        accumOcclusion += candidateOcclusion[bestIndex] * selectedWeight;
    }

    if (accumWeight <= 1e-4) {
        float3 fallbackAccum = float3(0.0);
        float fallbackWeight = 0.0;
        float fallbackOccAccum = 0.0;
        for (int candidate = 0; candidate < 8; ++candidate) {
            fallbackAccum += candidateSamples[candidate] * fallbackWeights[candidate];
            fallbackWeight += fallbackWeights[candidate];
            fallbackOccAccum += fallbackOcclusion[candidate] * fallbackWeights[candidate];
        }
        if (fallbackWeight <= 1e-4) {
            return float4(0.0);
        }
        float fallbackOcc = fallbackOccAccum / fallbackWeight;
        return float4(max(fallbackAccum / fallbackWeight, float3(0.0)), saturate(fallbackWeight * fallbackOcc));
    }
    float reflectionOcclusion = accumOcclusion / accumWeight;
    return float4(max(accum / accumWeight, float3(0.0)), saturate(accumWeight * reflectionOcclusion));
}

fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant MaterialUniforms& material [[buffer(1)]],
    constant LightData& light [[buffer(2)]],
    constant EnvironmentUniforms& environment [[buffer(3)]],
    const device LightGPUData* lights [[buffer(4)]],
    const device ShadowGPUData* shadowData [[buffer(5)]],
    constant uint& lightCount [[buffer(6)]],
    const device ClusterHeader* clusterHeaders [[buffer(7)]],
    const device uint* clusterIndices [[buffer(8)]],
    constant ClusterParams& clusterParams [[buffer(9)]],
    constant ProbeVolumeUniforms& probeVolume [[buffer(10)]],
    const device ProbeAmbientCubeData* probeData [[buffer(11)]],
    texture2d<float> albedoMap [[texture(0)]],
    texture2d<float> normalMap [[texture(1)]],
    texture2d<float> metallicMap [[texture(2)]],
    texture2d<float> roughnessMap [[texture(3)]],
    texture2d<float> aoMap [[texture(4)]],
    texture2d<float> emissionMap [[texture(5)]],
    texture2d<float> heightMap [[texture(6)]],
    texture2d<float> environmentMap [[texture(7)]],
    texturecube<float> irradianceMap [[texture(8)]],
    texturecube<float> prefilteredMap [[texture(9)]],
    texture2d<float> brdfLUT [[texture(10)]],
    depth2d<float> shadowAtlas [[texture(11)]],
    depth2d_array<float> pointShadowCube0 [[texture(12)]],
    depth2d_array<float> pointShadowCube1 [[texture(13)]],
    depth2d_array<float> pointShadowCube2 [[texture(14)]],
    depth2d_array<float> pointShadowCube3 [[texture(15)]],
    texture2d<float> ormMap [[texture(16)]],
    texture2d<float> ssaoMap [[texture(17)]],
    texture2d<float> decalAlbedoMap [[texture(18)]],
    texture2d<float> decalNormalMap [[texture(19)]],
    texture2d<float> decalOrmMap [[texture(20)]],
    texture2d<float> terrainControlMap [[texture(21)]],
    texture2d<float> terrainLayer0Map [[texture(22)]],
    texture2d<float> terrainLayer1Map [[texture(23)]],
    texture2d<float> terrainLayer2Map [[texture(24)]],
    texture2d<float> terrainLayer0NormalMap [[texture(25)]],
    texture2d<float> terrainLayer1NormalMap [[texture(26)]],
    texture2d<float> terrainLayer2NormalMap [[texture(27)]],
    texture2d<float> terrainLayer0OrmMap [[texture(28)]],
    texture2d<float> terrainLayer1OrmMap [[texture(29)]],
    texture2d<float> terrainLayer2OrmMap [[texture(30)]],
    texture2d<float> staticLightmap [[texture(31)]],
    texture2d<float> directionalStaticLightmap [[texture(32)]],
    texture2d<float> shadowmaskStaticLightmap [[texture(33)]],
    sampler textureSampler [[sampler(0)]],
    sampler environmentSampler [[sampler(1)]],
    sampler shadowSampler [[sampler(2)]]
) {
    float3 Vworld = normalize(camera.cameraPositionTime.xyz - in.worldPosition);
    float3 viewPos = (camera.viewMatrix * float4(in.worldPosition, 1.0)).xyz;
    float3 Vview = normalize(-viewPos);
    float3 T = normalize(in.tangent);
    float3 B = normalize(in.bitangent);
    float3 N = normalize(in.normal);
    float3x3 TBN = float3x3(T, B, N);
    float receiveShadows = material.heightParams.w;
    
    // View dir in tangent space for parallax / normal mapping
    float3 viewDirTS = normalize(float3(dot(Vworld, T), dot(Vworld, B), dot(Vworld, N)));
    
    float2 controlUV = clamp(in.texCoord, float2(0.0), float2(1.0));
    float2 uv = in.texCoord * material.uvTilingOffset.xy + material.uvTilingOffset.zw;
    
    // Parallax occlusion mapping with ray march + binary refinement
    if (material.textureFlags2.z > 0.5) {
        uv = applyParallaxOcclusionMapping(heightMap, textureSampler, uv, viewDirTS, material);
    }
    
    // Albedo
    float4 albedoSample = albedoMap.sample(textureSampler, uv);
    float3 vertexTint = (in.bakedLightingFlag > 0.5) ? float3(1.0) : in.color.rgb;
    float3 albedo = material.albedo.rgb * vertexTint;
    bool terrainEnabled = material.terrainParams0.x > 0.5 &&
        (material.terrainFlags.y + material.terrainFlags.z + material.terrainFlags.w) > 0.5;
    if (terrainEnabled) {
        float2 uv0 = uv * max(material.terrainLayer0ST.xy, float2(0.001));
        float2 uv1 = uv * max(material.terrainLayer1ST.xy, float2(0.001));
        float2 uv2 = uv * max(material.terrainLayer2ST.xy, float2(0.001));
        float3 weights = computeTerrainWeights(controlUV, in.worldPosition, N, material, terrainControlMap, textureSampler);
        float4 layer0 = terrainLayer0Map.sample(textureSampler, uv0);
        float4 layer1 = terrainLayer1Map.sample(textureSampler, uv1);
        float4 layer2 = terrainLayer2Map.sample(textureSampler, uv2);
        float4 terrainAlbedo = layer0 * weights.x + layer1 * weights.y + layer2 * weights.z;
        albedo *= terrainAlbedo.rgb;
        albedoSample = terrainAlbedo;
    } else if (material.textureFlags.x > 0.5) {
        albedo *= albedoSample.rgb;
    }
    float alpha = material.albedo.a * ((terrainEnabled || material.textureFlags.x > 0.5) ? albedoSample.a : 1.0);
    if (material.textureFlags3.y > 0.5 && alpha < material.textureFlags3.z) {
        discard_fragment();
    }
    if (material.foliageParams2.w > 0.5) {
        float fade = 1.0;
        if (material.foliageParams2.z > 0.5) {
            fade *= (in.billboardFlag > 0.5) ? in.billboardFade : (1.0 - in.billboardFade);
        }
        if (material.foliageParams2.y > 0.5) {
            fade *= (1.0 - in.lodFade);
        }
        if (fade < 0.999) {
            float2 p = floor(in.position.xy);
            float noise = fract(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
            if (noise > fade) {
                discard_fragment();
            }
        }
    }
    
    // Metallic/Roughness/AO
    float metallic = material.properties.x;
    float roughness = max(material.properties.y, 0.04);
    float ao = material.properties.z;
    float2 ndc = in.position.xy / in.position.w;
    float2 ssaoUV = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
    float4 decalClip = camera.projectionMatrixNoJitter * float4(viewPos, 1.0);
    float2 decalUV = float2(decalClip.x / decalClip.w * 0.5 + 0.5,
                            1.0 - (decalClip.y / decalClip.w * 0.5 + 0.5));
    float ssao = ssaoMap.sample(textureSampler, ssaoUV).r;
    ao = clamp(ao * ssao, 0.0, 1.0);
    
    if (terrainEnabled) {
        float2 uv0 = uv * max(material.terrainLayer0ST.xy, float2(0.001));
        float2 uv1 = uv * max(material.terrainLayer1ST.xy, float2(0.001));
        float2 uv2 = uv * max(material.terrainLayer2ST.xy, float2(0.001));
        float3 weights = computeTerrainWeights(controlUV, in.worldPosition, N, material, terrainControlMap, textureSampler);
        float3 orm0 = terrainLayer0OrmMap.sample(textureSampler, uv0).rgb;
        float3 orm1 = terrainLayer1OrmMap.sample(textureSampler, uv1).rgb;
        float3 orm2 = terrainLayer2OrmMap.sample(textureSampler, uv2).rgb;
        float3 terrainOrm = orm0 * weights.x + orm1 * weights.y + orm2 * weights.z;
        metallic = clamp(metallic * terrainOrm.b, 0.0, 1.0);
        roughness = clamp(roughness * terrainOrm.g, 0.04, 1.0);
        ao = clamp(ao * terrainOrm.r, 0.0, 1.0);
    } else if (material.textureFlags3.x > 0.5) {
        float3 orm = ormMap.sample(textureSampler, uv).rgb;
        if (material.textureFlags.z > 0.5) {
            metallic = clamp(metallic * metallicMap.sample(textureSampler, uv).r, 0.0, 1.0);
        } else {
            metallic = clamp(metallic * orm.b, 0.0, 1.0);
        }
        if (material.textureFlags.w > 0.5) {
            roughness = clamp(roughness * roughnessMap.sample(textureSampler, uv).r, 0.04, 1.0);
        } else {
            roughness = clamp(roughness * orm.g, 0.04, 1.0);
        }
        if (material.textureFlags2.x > 0.5) {
            ao = clamp(ao * aoMap.sample(textureSampler, uv).r, 0.0, 1.0);
        } else {
            ao = clamp(ao * orm.r, 0.0, 1.0);
        }
    } else {
        if (material.textureFlags.z > 0.5) {
            metallic = clamp(metallic * metallicMap.sample(textureSampler, uv).r, 0.0, 1.0);
        }
        if (material.textureFlags.w > 0.5) {
            roughness = clamp(roughness * roughnessMap.sample(textureSampler, uv).r, 0.04, 1.0);
        }
        if (material.textureFlags2.x > 0.5) {
            ao = clamp(ao * aoMap.sample(textureSampler, uv).r, 0.0, 1.0);
        }
    }
    
    // Normalize vectors
    if (terrainEnabled) {
        float2 uv0 = uv * max(material.terrainLayer0ST.xy, float2(0.001));
        float2 uv1 = uv * max(material.terrainLayer1ST.xy, float2(0.001));
        float2 uv2 = uv * max(material.terrainLayer2ST.xy, float2(0.001));
        float3 weights = computeTerrainWeights(controlUV, in.worldPosition, N, material, terrainControlMap, textureSampler);
        float3 tn0 = terrainLayer0NormalMap.sample(textureSampler, uv0).xyz * 2.0 - 1.0;
        float3 tn1 = terrainLayer1NormalMap.sample(textureSampler, uv1).xyz * 2.0 - 1.0;
        float3 tn2 = terrainLayer2NormalMap.sample(textureSampler, uv2).xyz * 2.0 - 1.0;
        float3 terrainTN = normalize(tn0 * weights.x + tn1 * weights.y + tn2 * weights.z);
        terrainTN = normalize(float3(terrainTN.xy * material.properties.w, terrainTN.z));
        N = normalize(TBN * terrainTN);
    } else if (material.textureFlags.y > 0.5) {
        float3 tangentNormal = normalMap.sample(textureSampler, uv).xyz * 2.0 - 1.0;
        tangentNormal = normalize(float3(tangentNormal.xy * material.properties.w, tangentNormal.z));
        N = normalize(TBN * tangentNormal);
    }

    float4 decalAlbedoSample = decalAlbedoMap.sample(textureSampler, decalUV);
    if (decalAlbedoSample.a > 0.001) {
        albedo = mix(albedo, decalAlbedoSample.rgb, decalAlbedoSample.a);
    }

    float4 decalNormalSample = decalNormalMap.sample(textureSampler, decalUV);
    if (decalNormalSample.a > 0.001) {
        float3 decalNormal = normalize(decalNormalSample.xyz * 2.0 - 1.0);
        N = normalize(mix(N, decalNormal, decalNormalSample.a));
    }

    float4 decalOrmSample = decalOrmMap.sample(textureSampler, decalUV);
    if (decalOrmSample.a > 0.001) {
        ao = mix(ao, decalOrmSample.r, decalOrmSample.a);
        roughness = mix(roughness, clamp(decalOrmSample.g, 0.04, 1.0), decalOrmSample.a);
        metallic = mix(metallic, clamp(decalOrmSample.b, 0.0, 1.0), decalOrmSample.a);
    }

    float3 Nview = normalize((camera.viewMatrix * float4(N, 0.0)).xyz);
    float3 V = normalize(camera.cameraPositionTime.xyz - in.worldPosition);
    
    // Calculate reflectance at normal incidence (dielectric vs metal)
    float3 F0 = float3(0.04);
    F0 = mix(F0, albedo, metallic);
    int shadowDebugMode = int(round(environment.skyParams.y));
    float rawShadowAccum = 0.0;
    float rawShadowWeight = 0.0;
    float directionalShadowAccum = 0.0;
    float directionalShadowWeight = 0.0;
    float pointShadowAccum = 0.0;
    float pointShadowWeight = 0.0;
    int debugCascadeIndex = -1;
    int debugPointFace = -1;
    
    // Reflectance equation
    float3 Lo = float3(0.0);
    float4 shadowmaskSample = float4(1.0);
    if (in.staticLightmapFlag > 0.5) {
        shadowmaskSample = shadowmaskStaticLightmap.sample(textureSampler, saturate(in.lightmapTexCoord));
    }
    
    // Iterate dynamic lights if provided; otherwise fall back to legacy main light
    if (lightCount > 0 && clusterHeaders && clusterIndices) {
        // Cluster selection
        float2 screen = (ndc * 0.5 + 0.5) * float2(clusterParams.screenWidth, clusterParams.screenHeight);
        uint cx = min((uint)(screen.x / (clusterParams.screenWidth / clusterParams.clusterX)), clusterParams.clusterX - 1);
        uint cy = min((uint)(screen.y / (clusterParams.screenHeight / clusterParams.clusterY)), clusterParams.clusterY - 1);
        float viewDepth = -viewPos.z;
        float logDepth = log2(viewDepth / clusterParams.nearPlane) / log2(clusterParams.farPlane / clusterParams.nearPlane);
        uint cz = (uint)clamp(logDepth * clusterParams.clusterZ, 0.0, float(clusterParams.clusterZ - 1));
        uint clusterId = cz * (clusterParams.clusterX * clusterParams.clusterY) + cy * clusterParams.clusterX + cx;
        ClusterHeader header = clusterHeaders[clusterId];
        
        for (uint idx = 0; idx < header.count; ++idx) {
            uint li = clusterIndices[header.offset + idx];
            LightGPUData Ld = lights[li];
            int type = (int)round(Ld.directionType.w);
            
            float3 LdirVS = float3(0.0);
            float distance = 1.0;
            float attenuation = 1.0;
            
            if (type == 0) { // Directional
                LdirVS = normalize(-Ld.directionType.xyz);
            } else {
                float3 toLightVS = Ld.positionRange.xyz - viewPos;
                distance = length(toLightVS);
                if (distance < 1e-4) continue;
                LdirVS = normalize(toLightVS);
                float range = (Ld.positionRange.w > 0.0) ? (1.0 / Ld.positionRange.w) : 0.0;
                float smooth = pow(saturate(1.0 - pow(distance / max(range, 0.0001), 4.0)), 2.0);
                attenuation = smooth / max(distance * distance, 0.001);
                
                if (type == 2) { // Spot
                    float cosTheta = dot(normalize(Ld.directionType.xyz), -LdirVS);
                    if (cosTheta < Ld.misc.y) continue;
                    float spotAtten = smoothstep(Ld.misc.y, Ld.misc.x, cosTheta);
                    attenuation *= spotAtten;
                } else if (type == 3 || type == 4) { // Area approx
                    attenuation *= 1.0 / max(distance, 0.1);
                }
            }
            
            float NdotL = max(dot(Nview, LdirVS), 0.0);
            if (NdotL <= 0.0 || attenuation <= 0.0) {
                continue;
            }
            
            float3 H = normalize(Vview + LdirVS);
            float NDF = DistributionGGX(Nview, H, roughness);
            float G = GeometrySmith(Nview, Vview, LdirVS, roughness);
            float3 F = fresnelSchlick(max(dot(H, Vview), 0.0), F0);
            
            float3 numerator = NDF * G * F;
            float denom = 4.0 * max(dot(Nview, Vview), 0.0) * max(NdotL, 0.0) + 0.001;
            float3 specular = numerator / denom;
            
            float3 kS = F;
            float3 kD = (float3(1.0) - kS) * (1.0 - metallic);
            
            uint lightFlags = (uint)round(Ld.shadowCookie.w);
            bool usePCSS = (lightFlags & 1u) != 0u;
            bool useContact = (lightFlags & 2u) != 0u;
            bool bakedDirect = (lightFlags & 8u) != 0u;
            uint mobility = (lightFlags >> 4u) & 0x3u;
            int shadowmaskChannel = int((lightFlags >> 6u) & 0x7u) - 1;
            int shadowIdx = (int)round(Ld.shadowCookie.x);
            int cascadeCount = (type == 0 && Ld.shadowCookie.z >= 1.0) ? (int)round(Ld.shadowCookie.z) : 1;
            float3 LdirWorld = normalize((camera.viewMatrixInverse * float4(LdirVS, 0.0)).xyz);
            float shadow = 1.0;
            if (shadowIdx >= 0 && receiveShadows > 0.5) {
                if (type == 1) {
                    float3 lightPosWS = (camera.viewMatrixInverse * float4(Ld.positionRange.xyz, 1.0)).xyz;
                    ShadowGPUData sLocal = shadowData[shadowIdx];
                    PointShadowProjection debugProj = projectPointShadowFace(in.worldPosition - lightPosWS);
                    debugPointFace = debugProj.face;
                    if (kDebugPointShadowView) {
                        float3 toLight = lightPosWS - in.worldPosition;
                        float nDotLDbg = saturate(dot(N, normalize(toLight)));
                        float texelWorldDbg = (2.0 * sLocal.depthRange.y) / max(sLocal.atlasUV.x, 1.0);
                        float receiverOffsetDbg = texelWorldDbg * mix(2.5, 1.0, nDotLDbg);
                        float3 samplePosDbg = in.worldPosition + normalize(N) * receiverOffsetDbg;
                        float3 toFragDbg = samplePosDbg - lightPosWS;
                        PointShadowProjection projDbg = projectPointShadowFace(toFragDbg);
                        float nearDbg = sLocal.depthRange.x;
                        float farDbg = sLocal.depthRange.y;
                        float refDbg = saturate((length(toFragDbg) - nearDbg) / max(farDbg - nearDbg, 1e-5));
                        float biasDbg = sLocal.params.x + sLocal.params.y * (1.0 - nDotLDbg);
                        refDbg = max(refDbg - biasDbg, 0.0);
                        uint cubeIndexDbg = (uint)round(sLocal.depthRange.z);
                        uint sliceDbg = cubeIndexDbg * 6u + projDbg.face;
                        int tierDbg = (int)round(sLocal.depthRange.w);
                        depth2d_array<float> cubeDbg = (tierDbg == 1) ? pointShadowCube1 : (tierDbg == 2 ? pointShadowCube2 : (tierDbg == 3 ? pointShadowCube3 : pointShadowCube0));
                        float sampleDepthDbg = cubeDbg.sample(shadowSampler, projDbg.uv, sliceDbg);
                        float shadowDbg = (refDbg <= sampleDepthDbg) ? 1.0 : 0.0;
                        float faceDbg = float(projDbg.face) / 5.0;
                        return float4(refDbg, sampleDepthDbg, faceDbg * 0.85 + shadowDbg * 0.15, 1.0);
                    }
                    shadow = samplePointShadow(shadowData, shadowIdx, in.worldPosition, N, lightPosWS, LdirWorld, sLocal.depthRange.w, pointShadowCube0, pointShadowCube1, pointShadowCube2, pointShadowCube3, shadowSampler);
                } else {
                    float viewDepth = max(-viewPos.z, 0.0);
                    debugCascadeIndex = resolveDirectionalCascadeIndex(shadowData, shadowIdx, cascadeCount, viewDepth);
                    shadow = sampleShadow(shadowData, shadowIdx, cascadeCount, viewDepth, in.worldPosition, N, LdirWorld, shadowAtlas, shadowSampler, usePCSS, useContact);
                }
                rawShadowAccum += shadow;
                rawShadowWeight += 1.0;
                if (type == 1) {
                    pointShadowAccum += shadow;
                    pointShadowWeight += 1.0;
                } else if (type == 0) {
                    directionalShadowAccum += shadow;
                    directionalShadowWeight += 1.0;
                }
            }

            bool directBakedLightmap = in.hdrStaticLightmapFlag >= 10.0;
            bool bakedStaticReceiver = in.staticLightmapFlag > 0.5 && directBakedLightmap && bakedDirect;
            if (bakedStaticReceiver) {
                if (mobility == 0u) {
                    continue;
                }
                if (mobility == 1u) {
                    if (shadowmaskChannel < 0 || shadowmaskChannel > 3) {
                        continue;
                    }
                    float shadowmaskVisibility = 1.0;
                    switch (shadowmaskChannel) {
                        case 0: shadowmaskVisibility = shadowmaskSample.x; break;
                        case 1: shadowmaskVisibility = shadowmaskSample.y; break;
                        case 2: shadowmaskVisibility = shadowmaskSample.z; break;
                        case 3: shadowmaskVisibility = shadowmaskSample.w; break;
                    }
                    shadow = (receiveShadows > 0.5) ? shadowmaskVisibility : 1.0;
                }
            }
            
            float3 radiance = Ld.colorIntensity.rgb * Ld.colorIntensity.w * attenuation * shadow;
            Lo += (kD * albedo / PI + specular) * radiance * NdotL;
        }
    } else {
        // Legacy single directional light
        float3 L = normalize(-light.direction.xyz);
        float3 H = normalize(V + L);
        
        float NdotL = max(dot(N, L), 0.0);
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        float3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * max(NdotL, 0.0) + 0.001;
        float3 specular = numerator / denom;
        
        float3 kS = F;
        float3 kD = (float3(1.0) - kS) * (1.0 - metallic);
        
        float3 radiance = light.color.xyz * light.direction.w;
        Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    }
    
    // ========================================
    // ENVIRONMENT LIGHTING (IBL)
    // ========================================
    float3 environmentLighting = float3(0.0);
    float3 environmentDiffuseLighting = float3(0.0);
    float3 environmentSpecularLighting = float3(0.0);
    float NdotV = max(dot(N, V), 0.0);
    float3 R = reflect(-V, N);
    
    // Check if we have proper IBL textures (environment.toneControl.y)
    bool hasProperIBL = environment.toneControl.y > 0.5;
    
    if (hasProperIBL) {
        // ========================================
        // PROPER IBL with precomputed maps
        // ========================================
        
        // Apply environment rotation to directions
        float3x3 envRot = environmentRotation(environment);
        float3 rotatedN = envRot * N;
        float3 rotatedR = envRot * R;
        
        // Diffuse IBL - sample irradiance cubemap
        float3 irradiance = irradianceMap.sample(environmentSampler, rotatedN).rgb;
        irradiance = applyEnvironmentGrading(irradiance, environment);
        
        // Specular IBL - sample prefiltered cubemap at roughness-based mip level
        const float MAX_REFLECTION_LOD = 5.0; // 6 mip levels (0-5)
        float3 prefilteredColor = prefilteredMap.sample(environmentSampler, rotatedR, level(roughness * MAX_REFLECTION_LOD)).rgb;
        prefilteredColor = applyEnvironmentGrading(prefilteredColor, environment);
        
        // BRDF integration lookup
        float2 envBRDF = brdfLUT.sample(environmentSampler, float2(NdotV, roughness)).rg;
        
        // Fresnel with roughness consideration
        float3 F_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kS = F_ibl;
        float3 kD = (1.0 - kS) * (1.0 - metallic);
        
        // Combine diffuse and specular IBL
        environmentDiffuseLighting = kD * irradiance * albedo;
        environmentSpecularLighting = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);
    } else {
        // ========================================
        // FALLBACK IBL with LOD-based sampling
        // ========================================
        float3 kS_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD_ibl = (float3(1.0) - kS_ibl) * (1.0 - metallic);
        
        // Diffuse - heavily blurred environment
        float3 irradiance = sampleEnvironment(environmentMap, environmentSampler, N, 6.0, environment);
        float3 diffuseIBL = irradiance * albedo;
        
        // Specular - roughness-based blur
        float roughLod = roughness * 6.0;
        float3 specularIBL = sampleEnvironment(environmentMap, environmentSampler, R, roughLod, environment);
        
        // Approximate BRDF integration
        float2 envBRDF = float2(1.0 - roughness, 0.0); // Rough approximation
        specularIBL *= (kS_ibl * envBRDF.x + envBRDF.y);
        
        environmentDiffuseLighting = kD_ibl * diffuseIBL;
        environmentSpecularLighting = specularIBL;
    }
    
    // AO should mostly shape indirect lighting, but full-strength AO often causes muddy shadows.
    float indirectAO = mix(1.0, ao, 0.82);
    environmentDiffuseLighting *= environment.exposureIntensity.y * indirectAO;
    environmentSpecularLighting *= environment.exposureIntensity.y;
    
    // Artist-controlled ambient fill in physically-plausible diffuse form.
    float3 ambientRadiance = environment.ambientColorIntensity.rgb * environment.ambientColorIntensity.w;
    float3 F_ambient = fresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD_ambient = (float3(1.0) - F_ambient) * (1.0 - metallic);
    float ambientAO = mix(1.0, ao, 0.65);
    float3 ambientLighting = ambientRadiance * (kD_ambient * albedo / PI) * ambientAO;
    float3 probeLighting = float3(0.0);
    if (in.staticLightmapFlag < 0.5 && probeVolume.gridCounts.w > 0.5 && probeVolume.featureParams.x > 0.5) {
        probeLighting = sample_probe_volume_irradiance(probeData, probeVolume, in.worldPosition, N);
    }
    float probeAO = mix(1.0, ao, 0.72);
    float3 probeIndirect = probeLighting * (kD_ambient * albedo / PI) * probeAO;
    float2 probeBRDF = hasProperIBL
        ? brdfLUT.sample(environmentSampler, float2(NdotV, roughness)).rg
        : float2(1.0 - roughness, 0.0);
    float4 localReflectionSample = sample_probe_volume_reflection(probeData, probeVolume, in.worldPosition, R, N, roughness);
    float localReflectionWeight = localReflectionSample.a * saturate(probeVolume.featureParams.z);
    float specularOcclusionStrength = clamp(probeVolume.blendParams.w, 0.0, 2.0);
    float specularAO = mix(1.0, compute_specular_occlusion(ao, NdotV, roughness), saturate(specularOcclusionStrength));
    if (specularOcclusionStrength > 1.0) {
        specularAO = pow(max(specularAO, 1e-3), specularOcclusionStrength);
    }
    float probeSpecularOcclusion = 1.0;
    if (probeVolume.blendParams.z > 0.5 && probeVolume.featureParams.y > 0.5 && localReflectionSample.a > 1e-4) {
        probeSpecularOcclusion = mix(1.0, saturate(localReflectionSample.a), 0.65);
    }
    float3 localReflectionLighting = localReflectionSample.rgb * (F0 * probeBRDF.x + probeBRDF.y) * specularAO;
    localReflectionLighting *= probeSpecularOcclusion;
    environmentSpecularLighting *= specularAO * probeSpecularOcclusion;
    environmentSpecularLighting = mix(environmentSpecularLighting, localReflectionLighting, saturate(localReflectionWeight));
    environmentLighting = environmentDiffuseLighting + environmentSpecularLighting;
    
    float3 bakedDirect = float3(0.0);
    if (in.bakedLightingFlag > 0.5) {
        float3 bakedKD = (float3(1.0) - F0) * (1.0 - metallic);
        bakedDirect = bakedKD * albedo / PI * max(in.color.rgb, float3(0.0));
    }
    if (in.staticLightmapFlag > 0.5) {
        float2 lightmapUV = clamp(in.lightmapTexCoord, float2(0.0), float2(1.0));
        float lightmapEncoding = in.hdrStaticLightmapFlag;
        bool directBakedLightmap = lightmapEncoding >= 10.0;
        if (directBakedLightmap) {
            lightmapEncoding -= 10.0;
        }
        float3 bakedRadiance = staticLightmap.sample(textureSampler, lightmapUV).rgb;
        if (lightmapEncoding < 0.5) {
            bakedRadiance *= 16.0;
        } else if (lightmapEncoding > 1.5) {
            float4 rgbmSample = staticLightmap.sample(textureSampler, lightmapUV);
            bakedRadiance = rgbmSample.rgb * rgbmSample.a * 16.0;
        }
        bakedRadiance = max(bakedRadiance, float3(0.0));
        float3 bakedKD = (float3(1.0) - F0) * (1.0 - metallic);
        if (directBakedLightmap) {
            float4 directionalSample = directionalStaticLightmap.sample(textureSampler, lightmapUV);
            float3 dominantVector = directionalSample.rgb * 2.0 - 1.0;
            float directionality = saturate(length(dominantVector));
            float referenceNoL = max(directionalSample.a, 0.05);
            float3 dominantDirection = float3(0.0, 0.0, 1.0);
            if (directionality > 0.001) {
                dominantDirection = dominantVector / directionality;
                float relitNoL = saturate(dot(N, dominantDirection));
                float directionalFactor = clamp(relitNoL / referenceNoL, 0.0, 2.0);
                bakedRadiance *= mix(1.0, directionalFactor, directionality);
            }
            bakedDirect += bakedKD * albedo / PI * bakedRadiance * mix(1.0, 1.08, directionality);
            if (directionality > 0.001) {
                float bakedNoL = saturate(dot(N, dominantDirection));
                if (bakedNoL > 0.0) {
                    float3 bakedIncidentRadiance = bakedRadiance / referenceNoL;
                    float3 bakedHalf = normalize(V + dominantDirection);
                    float bakedNDF = DistributionGGX(N, bakedHalf, roughness);
                    float bakedG = GeometrySmith(N, V, dominantDirection, roughness);
                    float3 bakedF = fresnelSchlick(max(dot(bakedHalf, V), 0.0), F0);
                    float3 bakedNumerator = bakedNDF * bakedG * bakedF;
                    float bakedDenom = 4.0 * max(dot(N, V), 0.0) * max(bakedNoL, 0.0) + 0.001;
                    float3 bakedSpecular = bakedNumerator / bakedDenom;
                    float bakedSpecularScale = directionality * mix(2.2, 1.1, roughness);
                    bakedDirect += bakedSpecular * bakedIncidentRadiance * bakedNoL * bakedSpecularScale;
                }
            }
        } else {
            bakedDirect += bakedKD * albedo / PI * bakedRadiance;
        }
    }

    // Add emission (unpack from Vector4)
    float3 emission = material.emission.xyz;
    if (material.textureFlags2.y > 0.5) {
        emission *= emissionMap.sample(textureSampler, uv).rgb;
    }
    emission *= material.emission.w; // emissionStrength in w
    
    // Final color - dynamic direct + baked direct + IBL + ambient fill + emission
    float3 color = Lo + bakedDirect + environmentLighting + ambientLighting + probeIndirect + emission;
    
    // ========== DEBUG: Check shadow map content ==========
    #if 0
    if (lightCount > 0 && clusterHeaders && clusterIndices) {
        float2 ndcDbg = in.position.xy / in.position.w;
        float2 screenDbg = (ndcDbg * 0.5 + 0.5) * float2(clusterParams.screenWidth, clusterParams.screenHeight);
        uint cxDbg = min((uint)(screenDbg.x / (clusterParams.screenWidth / clusterParams.clusterX)), clusterParams.clusterX - 1);
        uint cyDbg = min((uint)(screenDbg.y / (clusterParams.screenHeight / clusterParams.clusterY)), clusterParams.clusterY - 1);
        float viewDepthDbg = -viewPos.z;
        float logDepthDbg = log2(viewDepthDbg / clusterParams.nearPlane) / log2(clusterParams.farPlane / clusterParams.nearPlane);
        uint czDbg = (uint)clamp(logDepthDbg * clusterParams.clusterZ, 0.0, float(clusterParams.clusterZ - 1));
        uint clusterIdDbg = czDbg * (clusterParams.clusterX * clusterParams.clusterY) + cyDbg * clusterParams.clusterX + cxDbg;
        ClusterHeader headerDbg = clusterHeaders[clusterIdDbg];
        
        for (uint idxDbg = 0; idxDbg < headerDbg.count; ++idxDbg) {
            uint liDbg = clusterIndices[headerDbg.offset + idxDbg];
            LightGPUData LdDbg = lights[liDbg];
            int shadowIdxDbg = (int)round(LdDbg.shadowCookie.x);
            
            if (shadowIdxDbg >= 0) {
                ShadowGPUData sDbg = shadowData[shadowIdxDbg];
                
                float4 clipDbg = sDbg.viewProj * float4(in.worldPosition, 1.0);
                float3 ndcShadow = clipDbg.xyz / clipDbg.w;
                
                bool allInBounds = (ndcShadow.x >= -1.0 && ndcShadow.x <= 1.0 &&
                                    ndcShadow.y >= -1.0 && ndcShadow.y <= 1.0 &&
                                    ndcShadow.z >= 0.0 && ndcShadow.z <= 1.0);
                
                if (allInBounds) {
                    float2 uvDbg = float2(ndcShadow.x * 0.5 + 0.5, 0.5 - ndcShadow.y * 0.5);
                    uvDbg = uvDbg * sDbg.atlasUV.zw + sDbg.atlasUV.xy;
                    
                    // SAMPLE THE SHADOW MAP - what's actually in there?
                    float sampledDepth = shadowAtlas.sample(shadowSampler, uvDbg);
                    float fragDepth = ndcShadow.z;
                    
                    // Show sampled depth as grayscale
                    // If shadow map is empty, this will be 1.0 (white)
                    // If something was rendered, we should see darker values
                    return float4(sampledDepth, sampledDepth, sampledDepth, 1.0);
                    
                    // Alternative: show fragDepth vs sampledDepth
                    // Red = fragDepth, Green = sampledDepth
                    // return float4(fragDepth, sampledDepth, 0.0, 1.0);
                }
                
                // Out of bounds - show cyan
                return float4(0.0, 1.0, 1.0, 1.0);
            }
        }
    }
    // No shadow light - magenta
    return float4(1.0, 0.0, 1.0, 1.0);
    #endif
    // ========== END DEBUG ==========
    
    if (shadowDebugMode > 0) {
        if (shadowDebugMode == 1) {
            float v = (rawShadowWeight > 0.0) ? (rawShadowAccum / rawShadowWeight) : 1.0;
            return float4(v, v, v, 1.0);
        }
        if (shadowDebugMode == 2) {
            float v = (directionalShadowWeight > 0.0) ? (directionalShadowAccum / directionalShadowWeight) : 1.0;
            return float4(v, v, v, 1.0);
        }
        if (shadowDebugMode == 3) {
            float v = (pointShadowWeight > 0.0) ? (pointShadowAccum / pointShadowWeight) : 1.0;
            return float4(v, v, v, 1.0);
        }
        if (shadowDebugMode == 4) {
            return float4((debugCascadeIndex >= 0) ? shadowDebugCascadeColor(debugCascadeIndex) : float3(0.0), 1.0);
        }
        if (shadowDebugMode == 5) {
            return float4((debugPointFace >= 0) ? shadowDebugPointFaceColor(debugPointFace) : float3(0.0), 1.0);
        }
    }

    // Exposure control (EV)
    color *= pow(2.0, environment.exposureIntensity.x);
    
    if (environment.toneControl.w < 0.5) {
        // HDR tonemapping - ACES approximation for better colors
        color = color / (color + float3(0.6));
        color = color * 1.4; // Slight boost
        
        // Gamma correction
        color = pow(color, float3(1.0/2.2));
    }
    
    return float4(color, alpha);
}

// ============================================================================
// UNLIT SHADER (Simple colored)
// ============================================================================

fragment float4 fragment_unlit(
    VertexOut in [[stage_in]],
    constant MaterialUniforms& material [[buffer(1)]]
) {
    float3 color = material.albedo.rgb * in.color.rgb;
    color += material.emission.xyz * material.emission.w;
    
    return float4(color, material.albedo.a);
}

// ============================================================================ 
// SKYBOX SHADER - Full-screen triangle with ray direction
// ============================================================================

struct SkyboxVertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex SkyboxVertexOut skybox_vertex(uint vertexId [[vertex_id]]) {
    SkyboxVertexOut out;
    
    // Full-screen triangle (3 vertices cover entire screen)
    // Generates positions: (-1,-1), (3,-1), (-1,3)
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2) * 2.0 - 1.0;
    
    out.position = float4(pos, 0.9999, 1.0); // Near far plane
    out.uv = pos;
    return out;
}

fragment float4 skybox_fragment(
    SkyboxVertexOut in [[stage_in]],
    constant CameraUniforms& camera [[buffer(1)]],
    constant EnvironmentUniforms& environment [[buffer(0)]],
    constant LightData& sun [[buffer(2)]],
    texture2d<float> environmentMap [[texture(0)]],
    texturecube<float> environmentCubemap [[texture(1)]],
    sampler environmentSampler [[sampler(0)]]
) {
    if (environment.toneControl.z < 0.5) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    
    // Reconstruct world-space ray direction from screen UV
    float2 ndc = in.uv;
    
    // Get inverse projection parameters
    float tanHalfFovY = 1.0 / camera.projectionMatrixNoJitter[1][1];
    float tanHalfFovX = 1.0 / camera.projectionMatrixNoJitter[0][0];
    
    // View-space ray direction (camera looks down -Z)
    float3 viewRay = normalize(float3(ndc.x * tanHalfFovX, ndc.y * tanHalfFovY, -1.0));
    
    // Transform to world space - extract rotation from view matrix (transpose of upper 3x3)
    float3x3 invViewRot = transpose(float3x3(
        camera.viewMatrix[0].xyz,
        camera.viewMatrix[1].xyz,
        camera.viewMatrix[2].xyz
    ));
    
    float3 worldDir = normalize(invViewRot * viewRay);

    float3 color = float3(0.0);
    float useHDRI = environment.skyParams.x;
    if (useHDRI > 0.5) {
        bool hasRawEnvironment = environment.skyParams.z > 0.5;
        if (hasRawEnvironment) {
            color = sampleEnvironment(environmentMap, environmentSampler, worldDir, 0.0, environment);
        } else if (environment.toneControl.y > 0.5) {
            float3x3 envRot = environmentRotation(environment);
            float3 rotatedDir = envRot * worldDir;
            float maxLod = max(float(environmentCubemap.get_num_mip_levels() - 1), 0.0);
            float lod = clamp(environment.exposureIntensity.w, 0.0, maxLod);
            color = environmentCubemap.sample(environmentSampler, rotatedDir, level(lod)).rgb;
            color = applyEnvironmentGrading(color, environment);
        } else {
            color = sampleEnvironment(environmentMap, environmentSampler, worldDir, 0.0, environment);
        }
    } else {
        float3 sunDir = normalize(-sun.direction.xyz);
        float sunIntensity = max(sun.direction.w, 0.0);
        float sunHeight = clamp(sunDir.y * 0.5 + 0.5, 0.0, 1.0);

        float3 zenithColor = mix(float3(0.08, 0.18, 0.35), float3(0.15, 0.35, 0.65), sunHeight);
        float3 horizonColor = mix(float3(0.7, 0.8, 0.9), float3(1.0, 0.6, 0.4), pow(1.0 - sunHeight, 2.0));
        float t = clamp(worldDir.y * 0.5 + 0.5, 0.0, 1.0);
        float3 skyColor = mix(horizonColor, zenithColor, pow(t, 0.7));

        float sunDot = max(dot(worldDir, sunDir), 0.0);
        float sunAngularRadius = 0.004675f; // ~0.53deg diameter
        float sunCosOuter = cos(sunAngularRadius * 1.5);
        float sunCosInner = cos(sunAngularRadius * 0.75);
        float sunCore = smoothstep(sunCosOuter, sunCosInner, sunDot);
        float sunGlow = pow(sunDot, 96.0);
        float3 sunWarm = float3(1.0, 0.55, 0.35);
        float3 sunCool = float3(1.0, 0.95, 0.9);
        float3 sunColor = sun.color.xyz * mix(sunWarm, sunCool, sunHeight);
        float3 sunRadiance = sunColor * (sunCore * 8.0 + sunGlow * 0.35) * sunIntensity;

        color = applyEnvironmentGrading(skyColor + sunRadiance, environment);
    }
    color *= environment.exposureIntensity.z;           // sky intensity
    color *= pow(2.0, environment.exposureIntensity.x); // exposure EV
    
    if (environment.toneControl.w < 0.5) {
        // Tonemap + gamma for LDR output.
        color = color / (color + float3(0.6));
        color = color * 1.4;
        color = pow(color, float3(1.0/2.2));
    }
    
    return float4(color, 1.0);
}

// ============================================================================
// BLIT SHADER - Full-screen triangle to present offscreen color
// ============================================================================

vertex BlitVertexOut blit_vertex(uint vertexId [[vertex_id]]) {
    BlitVertexOut out;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2) * 2.0 - 1.0;
    out.position = float4(pos, 0.0, 1.0);
    out.uv = float2(pos.x * 0.5 + 0.5, 1.0 - (pos.y * 0.5 + 0.5));
    return out;
}

fragment float4 blit_fragment(
    BlitVertexOut in [[stage_in]],
    constant PostProcessParams& params [[buffer(0)]],
    texture2d<float> source [[texture(0)]],
    texture2d<float> lutTex [[texture(1)]],
    sampler sourceSampler [[sampler(0)]]
) {
    float3 color = source.sample(sourceSampler, in.uv).rgb;
    float toneMapping = params.params1.y;
    float inputIsLinear = params.params1.z;
    if (toneMapping > 0.5) {
        if (toneMapping < 1.5) {
            color = tonemapFilmic(color);
        } else {
            color = tonemapACES(color);
        }
    } else if (inputIsLinear > 0.5) {
        // If the scene is still linear HDR, apply display gamma at minimum.
        color = pow(max(color, float3(0.0)), float3(1.0 / 2.2));
    }
    float gradingIntensity = clamp(params.params1.x, 0.0, 1.0);
    if (gradingIntensity > 0.001) {
        float3 graded = sampleLUT2D(color, lutTex, sourceSampler);
        color = mix(color, graded, gradingIntensity);
    }
    float vignetteIntensity = clamp(params.params0.x, 0.0, 1.0);
    if (vignetteIntensity > 0.001) {
        float2 uv = in.uv - 0.5;
        float vig = smoothstep(0.4, 0.9, dot(uv, uv));
        color *= mix(1.0, 1.0 - vignetteIntensity, vig);
    }
    float grainIntensity = clamp(params.params0.y, 0.0, 1.0);
    if (grainIntensity > 0.0001) {
        float grainScale = max(params.params0.z, 1.0);
        float t = params.params0.w;
        float2 uv = in.uv * grainScale + float2(t * 0.17, t * 0.29);
        float noise = fract(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
        float grain = (noise - 0.5) * 2.0;
        color = max(color + grain * grainIntensity, 0.0);
    }
    if (grainIntensity < 0.0001) {
        float ditherAmount = (toneMapping > 0.5 || gradingIntensity > 0.001) ? 1.0 : 0.0;
        if (ditherAmount > 0.0) {
            float2 pixel = in.uv * float2(source.get_width(), source.get_height());
            color = applyDither(color, pixel, ditherAmount);
        }
    }
    return float4(color, 1.0);
}
