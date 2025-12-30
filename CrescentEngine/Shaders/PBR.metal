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
};

struct VertexInSkinned {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
    float3 tangent [[attribute(3)]];
    float3 bitangent [[attribute(4)]];
    float4 color [[attribute(5)]];
    uint4 boneIndices [[attribute(6)]];
    float4 boneWeights [[attribute(7)]];
};

// Vertex output / Fragment input
struct VertexOut {
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
    float2 texCoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

struct PrepassOut {
    float4 position [[position]];
    float3 normalVS;
    float2 texCoord;
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
    float4 params1; // gradingIntensity, toneMapping, padding, padding
};

struct TAAParams {
    float4x4 prevViewProjection;
    float4 params0; // texelSize.xy, feedback, historyValid
    float4 params1; // sharpness, useVelocity, depthThreshold, normalThreshold
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
    float4 view = camera.projectionMatrixNoJitterInverse * clip;
    return view.xyz / max(view.w, 0.0001);
}

inline float linearizeDepth(float depth, constant CameraUniforms& camera) {
    float a = camera.projectionMatrix[2][2];
    float b = camera.projectionMatrix[3][2];
    float viewZ = b / (-depth - a);
    return -viewZ;
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
    float3(-0.6999, -0.0451, -0.0019),
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

inline float2 rotate2(float2 v, float2 r) {
    return float2(v.x * r.x - v.y * r.y, v.x * r.y + v.y * r.x);
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
    float2 texel = 1.0 / float2(atlas.get_width(), atlas.get_height());
    float2 margin = texel * (radius + 1.0);
    float2 minBounds = tileMin + margin;
    float2 maxBounds = tileMax - margin;
    float shadow = 0.0;
    float count = 0.0;
    for (int i = 0; i < 4; ++i) {
        float2 offset = rotate2(kPoissonDisk16[i], rot) * radius;
        float2 uvOffset = uv + offset * texel;
        if (uvOffset.x >= minBounds.x && uvOffset.x <= maxBounds.x &&
            uvOffset.y >= minBounds.y && uvOffset.y <= maxBounds.y) {
            float sampleDepth = atlas.sample(shadowSampler, uvOffset);
            shadow += (depth - bias <= sampleDepth) ? 1.0 : 0.0;
            count += 1.0;
        }
    }
    return (count > 0.0) ? (shadow / count) : 1.0;
}

float pcfSample(depth2d<float> atlas,
                sampler shadowSampler,
                float2 uv,
                float depth,
                float bias,
                float2 tileMin,
                float2 tileMax,
                float2 rot,
                float radius) {
    float2 texel = 1.0 / float2(atlas.get_width(), atlas.get_height());
    float shadow = 0.0;
    float validSamples = 0.0;

    // Add small margin to prevent bleeding
    float2 margin = texel * (radius + 1.0);
    tileMin += margin;
    tileMax -= margin;

    for (int i = 0; i < 16; ++i) {
        float2 offset = rotate2(kPoissonDisk16[i], rot) * radius;
        float2 uvOffset = uv + offset * texel;

        // Check if sample is within tile bounds
        if (uvOffset.x >= tileMin.x && uvOffset.x <= tileMax.x &&
            uvOffset.y >= tileMin.y && uvOffset.y <= tileMax.y) {
            shadow += atlas.sample_compare(shadowSampler, uvOffset, depth - bias);
            validSamples += 1.0;
        }
    }

    // If we have valid samples, return average; otherwise assume lit (1.0)
    return (validSamples > 0.0) ? (shadow / validSamples) : 1.0;
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
    constexpr sampler shadowDepthSampler(filter::linear, address::clamp_to_edge);
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
    float2 filterMargin = texel * (radius + 1.0);
    minBounds = tileMin + filterMargin;
    maxBounds = tileMax - filterMargin;

    // Filter pass
    float shadow = 0.0;
    float validSamples = 0.0;
    for (int i = 0; i < 16; ++i) {
        float2 offset = rotate2(kPoissonDisk16[i], rot) * radius;
        float2 offsetUV = uv + offset * texel;
        if (offsetUV.x >= minBounds.x && offsetUV.x <= maxBounds.x &&
            offsetUV.y >= minBounds.y && offsetUV.y <= maxBounds.y) {
            shadow += atlas.sample_compare(shadowSampler, offsetUV, depth - bias);
            validSamples += 1.0;
        }
    }

    return (validSamples > 0.0) ? (shadow / validSamples) : 1.0;
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
                                 float baseTexelWorld) {
    ShadowGPUData s = shadowData[idx];
    float4 clip = s.viewProj * float4(pos, 1.0);
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
    float nDotL = saturate(dot(normalize(normalWS), normalize(-lightDirWS)));
    bias += s.params.y * (1.0 - nDotL); // normal bias for grazing angles

    float2 rot = float2(1.0, 0.0);
    if (usePCSS && s.params.w > 0.5) {
        float2 atlasSize = float2(atlas.get_width(), atlas.get_height());
        float2 noiseCoord = floor(uv * atlasSize);
        float randAngle = hash21(noiseCoord) * TWO_PI;
        rot = float2(cos(randAngle), sin(randAngle));
    }

    // Use tighter kernel for near cascade to reduce shimmer
    float kernelRadius = (s.params.w < 0.5) ? 1.5 : 2.5;

    // ndc.z is already 0..1 for our D3D/Metal-style projections
    float depth = ndc.z;

    float penumbra = s.params.z;
    float shadow = 1.0;
    bool usePCSSLocal = usePCSS && (s.params.w > 0.5);
    if (usePCSSLocal && penumbra > 0.0) {
        shadow = pcssSample(atlas, shadowSampler, uv, depth, bias, tileMin, tileMax, penumbra, rot, kernelRadius);
    } else {
        shadow = pcfSample(atlas, shadowSampler, uv, depth, bias, tileMin, tileMax, rot, kernelRadius);
    }

    if (useContact) {
        float contactBias = bias * 0.5;
        float contactRadius = 1.0;
        float contact = pcfSample(atlas, shadowSampler, uv, depth, contactBias, tileMin, tileMax, rot, contactRadius);
        float contactStrength = (s.params.w < 0.5) ? 0.4 : 0.6;
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

    if (cascadeCount <= 1) {
        return sampleShadowCascade(shadowData, shadowIdx, worldPos, normalWS, lightDirWS, atlas, shadowSampler, usePCSS, useContact, baseTexelWorld);
    }

    // Use view-space depth for cascade selection (matches split computation)
    viewDepth = max(viewDepth, 0.0);

    // Find the appropriate cascade based on depth
    int current = 0;
    for (int c = 0; c < cascadeCount; ++c) {
        ShadowGPUData s = shadowData[shadowIdx + c];
        if (viewDepth < s.depthRange.y) {
            current = c;
            break;
        }
        current = c;  // Use last cascade if beyond all ranges
    }

    // Blend with next cascade for smooth transitions
    int cascadeA = current;
    int cascadeB = current;
    float blend = 0.0;

    if (current < cascadeCount - 1) {
        ShadowGPUData curData = shadowData[shadowIdx + current];
        ShadowGPUData nextData = shadowData[shadowIdx + current + 1];

        // Adaptive blend range: larger for fewer cascades
        float cascadeRange = curData.depthRange.y - curData.depthRange.x;
        float blendFactor = min(0.3, 6.0 / float(cascadeCount)); // Wider blend for stability
        float blendRange = blendFactor * cascadeRange;
        float blendStart = curData.depthRange.y - blendRange;

        if (viewDepth > blendStart && viewDepth < nextData.depthRange.y) {
            cascadeB = current + 1;
            float t = saturate((viewDepth - blendStart) / blendRange);
            blend = smoothstep(0.0, 1.0, t);
        }
    }

    bool allowPCSS_A = usePCSS;
    if (cascadeCount > 1 && cascadeA <= 1) {
        allowPCSS_A = false;
    }
    float shadowA = sampleShadowCascade(shadowData, shadowIdx + cascadeA, worldPos, normalWS, lightDirWS, atlas, shadowSampler, allowPCSS_A, useContact, baseTexelWorld);
    if (blend < 0.001) return shadowA;  // Skip second sample if blend is negligible
    bool allowPCSS_B = usePCSS;
    if (cascadeCount > 1 && cascadeB <= 1) {
        allowPCSS_B = false;
    }
    float shadowB = sampleShadowCascade(shadowData, shadowIdx + cascadeB, worldPos, normalWS, lightDirWS, atlas, shadowSampler, allowPCSS_B, useContact, baseTexelWorld);
    return mix(shadowA, shadowB, blend);
}

float samplePointShadow(const device ShadowGPUData* shadowData,
                        int shadowIdx,
                        float3 worldPos,
                        float3 normalWS,
                        float3 lightPosWS,
                        float3 lightDirWS,
                        float tier,
                        depthcube_array<float> cube0,
                        depthcube_array<float> cube1,
                        depthcube_array<float> cube2,
                        depthcube_array<float> cube3,
                        sampler shadowSampler) {
    ShadowGPUData s = shadowData[shadowIdx];
    float3 toFrag = worldPos - lightPosWS;
    float3 dir = normalize(toFrag);
    
    // For cubemap shadows with perspective projection, we need to convert
    // linear distance to perspective depth. The dominant axis determines
    // which face we're sampling, and that axis value approximates view space Z.
    float3 absDir = abs(toFrag);
    float maxComp = max(absDir.x, max(absDir.y, absDir.z));
    
    // Perspective projection: z_ndc = (far / (near - far)) + (far * near) / (z * (near - far))
    // Since we're using positive distances, and Metal perspective has m[11] = -1:
    float nearP = s.depthRange.x;
    float farP = s.depthRange.y;
    float denom = max(farP - nearP, 1e-5);
    float refDist = max(maxComp, 1e-5);
    float ref = (farP / denom) - (farP * nearP) / (denom * refDist);
    ref = saturate(ref);
    
    float bias = s.params.x;
    float nDotL = saturate(dot(normalize(normalWS), normalize(-lightDirWS)));
    bias += s.params.y * (1.0 - nDotL);
    ref = max(ref - bias, 0.0);
    
    uint cubeIndex = (uint)round(s.depthRange.z);
    int t = (int)round(tier);
    depthcube_array<float> cubeTex = (t == 1) ? cube1 : (t == 2 ? cube2 : (t == 3 ? cube3 : cube0));
    
    // 9-tap angular PCF
    float shadow = 0.0;
    float3 up = abs(dir.y) < 0.99 ? float3(0,1,0) : float3(0,0,1);
    float3 tvec = normalize(cross(up, dir));
    float3 bvec = cross(dir, tvec);
    float angleRadius = 0.0035;
    float2 kernelOffsets[9] = {
        float2(0,0),
        float2(1,0), float2(-1,0),
        float2(0,1), float2(0,-1),
        float2(1,1), float2(-1,1),
        float2(1,-1), float2(-1,-1)
    };
    for (int i = 0; i < 9; ++i) {
        float2 k = kernelOffsets[i] * angleRadius;
        float3 offsetDir = normalize(dir + k.x * tvec + k.y * bvec);
        shadow += cubeTex.sample_compare(shadowSampler, offsetDir, ref, cubeIndex);
    }
    return shadow / 9.0;
}

// ============================================================================
// VERTEX SHADER
// ============================================================================

vertex PrepassOut vertex_prepass(
    VertexIn in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]]
) {
    PrepassOut out;
    float4 worldPos = model.modelMatrix * float4(in.position, 1.0);
    out.position = camera.viewProjectionMatrix * worldPos;
    float3 worldNormal = normalize((model.normalMatrix * float4(in.normal, 0.0)).xyz);
    out.normalVS = normalize((camera.viewMatrix * float4(worldNormal, 0.0)).xyz);
    out.texCoord = in.texCoord;
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
    float3x3 model3 = float3x3(model.modelMatrix[0].xyz,
                               model.modelMatrix[1].xyz,
                               model.modelMatrix[2].xyz);
    float3 worldNormal = normalize(model3 * (skin3 * in.normal));
    out.normalVS = normalize((camera.viewMatrix * float4(worldNormal, 0.0)).xyz);
    out.texCoord = in.texCoord;
    return out;
}

vertex VelocityOut vertex_velocity(
    VertexIn in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    constant VelocityUniforms& velocity [[buffer(5)]]
) {
    VelocityOut out;
    float4 worldPos = model.modelMatrix * float4(in.position, 1.0);
    float4 prevWorldPos = velocity.prevModelMatrix * float4(in.position, 1.0);
    float4 currClip = velocity.currViewProjection * worldPos;
    float4 prevClip = velocity.prevViewProjection * prevWorldPos;
    out.position = currClip;
    out.currClip = currClip;
    out.prevClip = prevClip;
    return out;
}

vertex VelocityOut vertex_velocity_skinned(
    VertexInSkinned in [[stage_in]],
    constant ModelUniforms& model [[buffer(1)]],
    const device float4x4* bones [[buffer(3)]],
    constant VelocityUniforms& velocity [[buffer(5)]],
    const device float4x4* prevBones [[buffer(6)]]
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
    constant CameraUniforms& camera [[buffer(2)]]
) {
    VertexOut out;
    
    // Transform to world space
    float4 worldPos = model.modelMatrix * float4(in.position, 1.0);
    out.worldPosition = worldPos.xyz;
    
    // Transform to clip space
    out.position = camera.viewProjectionMatrix * worldPos;
    
    // Transform normal, tangent, bitangent to world space
    out.normal = normalize((model.normalMatrix * float4(in.normal, 0.0)).xyz);
    out.tangent = normalize((model.modelMatrix * float4(in.tangent, 0.0)).xyz);
    out.bitangent = normalize((model.modelMatrix * float4(in.bitangent, 0.0)).xyz);
    
    // Pass through
    out.texCoord = in.texCoord;
    out.color = in.color;
    
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
    float3x3 model3 = float3x3(model.modelMatrix[0].xyz,
                               model.modelMatrix[1].xyz,
                               model.modelMatrix[2].xyz);

    out.normal = normalize(model3 * (skin3 * in.normal));
    out.tangent = normalize(model3 * (skin3 * in.tangent));
    out.bitangent = normalize(model3 * (skin3 * in.bitangent));

    out.texCoord = in.texCoord;
    out.color = in.color;

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
    sampler textureSampler [[sampler(0)]]
) {
    float3 n = normalize(in.normalVS);
    float2 uv = in.texCoord * material.uvTilingOffset.xy + material.uvTilingOffset.zw;
    float roughness = clamp(material.properties.y, 0.04, 1.0);
    if (material.textureFlags3.x > 0.5) {
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

    float2 noiseUV = fract(in.uv * params.noiseScale);
    float3 noise = normalize(noiseTex.sample(ssaoSampler, noiseUV).xyz * 2.0 - 1.0);
    float3 tangent = normalize(noise - normalVS * dot(noise, normalVS));
    float3 bitangent = cross(normalVS, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normalVS);

    float occlusion = 0.0;
    float radius = params.settings.x;
    float bias = params.settings.y;

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
    float soft = 0.0;
    if (knee > 0.0) {
        soft = saturate((brightness - threshold + knee) / (2.0 * knee));
    }
    float contrib = max(brightness - threshold, 0.0) + soft * soft * knee;
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
    if (toneMapping > 0.5) {
        if (toneMapping < 1.5) {
            color = tonemapFilmic(color);
        } else {
            color = tonemapACES(color);
        }
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
        color += grain * grainIntensity;
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
    float hit = 0.0;

    for (int i = 0; i < 128; ++i) {
        if (float(i) >= maxSteps) {
            break;
        }
        float3 rayPos = viewPos + reflDir * (stepSize * (float(i) + 1.0));
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
            if (rayPos.z <= sampleView.z + thickness) {
                float3 start = prevPos;
                float3 end = rayPos;
                float2 refineUV = uv;
                for (int j = 0; j < 4; ++j) {
                    float3 mid = (start + end) * 0.5;
                    float4 midClip = camera.projectionMatrix * float4(mid, 1.0);
                    float2 midUV = float2(midClip.x / midClip.w * 0.5 + 0.5,
                                          1.0 - (midClip.y / midClip.w * 0.5 + 0.5));
                    float midDepth = depthTex.sample(sourceSampler, midUV);
                    float3 midView = reconstructViewPosition(midUV, midDepth, camera);
                    if (mid.z <= midView.z + thickness) {
                        end = mid;
                        refineUV = midUV;
                    } else {
                        start = mid;
                    }
                }
                hitUV = refineUV;
                hit = 1.0;
                break;
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
    float edgeFade = saturate(1.0 - max(edge.x, edge.y));
    float roughnessScale = saturate((maxRoughness - roughness) / max(maxRoughness, 0.0001));
    float dist = length(reconstructViewPosition(hitUV, depthTex.sample(sourceSampler, hitUV), camera) - viewPos);
    float distFade = saturate(1.0 - dist / max(params.settings1.w, 0.0001));
    float weight = hit * fresnel * edgeFade * distFade * roughnessScale;

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
        float3 T = normalize(decal.modelMatrix[0].xyz);
        float3 B = normalize(decal.modelMatrix[1].xyz);
        float3 N = normalize(decal.modelMatrix[2].xyz);
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
    }
    history = clamp(history, minC, maxC);

    float feedback = clamp(params.params0.z, 0.02, 0.25);
    float speedPx = length(velocity / texel);
    float motionFactor = saturate(1.0 - speedPx * 0.15);
    float stabilized = mix(feedback, 0.28, motionFactor);
    float prevDepth = depthTex.sample(sourceSampler, prevUV);
    float depthDiff = abs(prevDepth - depth);
    float depthWeight = 1.0 - smoothstep(0.0, params.params1.z, depthDiff);
    float3 normalNow = normalize(normalTex.sample(sourceSampler, in.uv).xyz * 2.0 - 1.0);
    float3 normalPrev = normalize(normalTex.sample(sourceSampler, prevUV).xyz * 2.0 - 1.0);
    float normalDot = clamp(dot(normalNow, normalPrev), 0.0, 1.0);
    float normalWeight = smoothstep(params.params1.w, 1.0, normalDot);
    feedback = stabilized * depthWeight * normalWeight;
    float3 color = mix(current, history, feedback);
    float sharpness = clamp(params.params1.x, 0.0, 1.0);
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
        float w = 1.0 - abs(t) * 2.0;
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

    float3 accum = current;
    float weightSum = 1.0;
    const float golden = 2.399963;
    for (int i = 0; i < maxSamples; ++i) {
        if (i >= sampleCount) {
            break;
        }
        float t = (float(i) + 0.5) / float(sampleCount);
        float r = radius * sqrt(t);
        float theta = float(i) * golden;
        float2 offset = float2(cos(theta), sin(theta)) * r;
        float2 uv = in.uv + offset * texel;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            continue;
        }
        float w = 1.0 - t * 0.5;
        accum += sceneTex.sample(sourceSampler, uv).rgb * w;
        weightSum += w;
    }
    float3 color = accum / weightSum;
    return float4(color, 1.0);
}

kernel void fog_volume_build(
    texture3d<float, access::write> volumeTex [[texture(0)]],
    texture3d<float, access::read> historyTex [[texture(1)]],
    depth2d<float> shadowAtlas [[texture(2)]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant FogParams& params [[buffer(1)]],
    const device ShadowGPUData* shadowData [[buffer(2)]],
    sampler shadowSampler [[sampler(0)]],
    uint3 tid [[thread_position_in_grid]]
) {
    uint width = volumeTex.get_width();
    uint height = volumeTex.get_height();
    uint depth = volumeTex.get_depth();
    if (tid.x >= width || tid.y >= height || tid.z >= depth) {
        return;
    }

    float2 uv = (float2(tid.x, tid.y) + 0.5) / float2(width, height);
    float slice = (float(tid.z) + 0.5) / float(depth);

    float nearPlane = max(params.volumeParams.x, 0.001);
    float farPlane = max(params.volumeParams.y, nearPlane + 0.001);
    float viewDepth = nearPlane * pow(farPlane / nearPlane, slice);

    float2 ndc = uv * 2.0 - 1.0;
    float4 clip = float4(ndc, 1.0, 1.0);
    float4 view = camera.projectionMatrixInverse * clip;
    float3 viewDir = normalize(view.xyz / max(view.w, 0.0001));
    float3 viewPos = viewDir * viewDepth;
    float3 worldPos = (camera.viewMatrixInverse * float4(viewPos, 1.0)).xyz;

    float baseDensity = clamp(params.fogColorDensity.w, 0.0, 1.0) * 0.08;
    float heightFactor = 1.0;
    if (params.misc.x > 0.5) {
        float falloff = max(params.distanceParams.w, 0.0001);
        heightFactor = exp(-falloff * (worldPos.y - params.distanceParams.z));
        heightFactor = clamp(heightFactor, 0.0, 1.0);
    }
    float extinction = baseDensity * heightFactor;
    float albedo = 0.8;
    float scatterStrength = max(params.sunColor.w, 0.0);
    float3 baseScattering = params.fogColorDensity.rgb * extinction * albedo * scatterStrength;
    float3 sunScattering = float3(0.0);

    float sunIntensity = clamp(params.sunDirIntensity.w, 0.0, 10.0);
    if (sunIntensity > 0.001) {
        float3 viewDirWorld = normalize(worldPos - camera.cameraPosition);
        float3 sunDir = normalize(-params.sunDirIntensity.xyz);
        float cosTheta = clamp(dot(viewDirWorld, sunDir), -1.0, 1.0);
        float g = clamp(params.misc.y, 0.0, 0.9);
        float phase = henyeyGreensteinPhase(cosTheta, g);
        sunScattering += params.sunColor.rgb * sunIntensity * phase * extinction * 0.2 * scatterStrength;
    }

    float shadowFactor = 1.0;
    if (params.shadowParams.z > 0.5 && params.shadowParams.y >= 1.0) {
        int shadowIndex = (int)round(params.shadowParams.x);
        int cascadeCount = (int)round(params.shadowParams.y);
        if (shadowIndex >= 0) {
            float3 viewPosShadow = (camera.viewMatrix * float4(worldPos, 1.0)).xyz;
            float viewDepth = max(-viewPosShadow.z, 0.0);
            int current = 0;
            for (int c = 0; c < cascadeCount; ++c) {
                ShadowGPUData s = shadowData[shadowIndex + c];
                if (viewDepth < s.depthRange.y) {
                    current = c;
                    break;
                }
                current = c;
            }
            ShadowGPUData s = shadowData[shadowIndex + current];
            float4 clip = s.viewProj * float4(worldPos, 1.0);
            float3 ndc = clip.xyz / clip.w;
            if (ndc.x >= -1.0 && ndc.x <= 1.0 && ndc.y >= -1.0 && ndc.y <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0) {
                float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
                float2 tileMin = s.atlasUV.xy;
                float2 tileMax = s.atlasUV.xy + s.atlasUV.zw;
                if (uv.x >= tileMin.x && uv.x <= tileMax.x && uv.y >= tileMin.y && uv.y <= tileMax.y) {
                    float bias = s.params.x * 0.5;
                    float randAngle = hash21(uv * 4096.0) * TWO_PI;
                    float2 rot = float2(cos(randAngle), sin(randAngle));
                    shadowFactor = sampleShadowDepthPCF(shadowAtlas, shadowSampler, uv, ndc.z, bias, tileMin, tileMax, rot, 1.5);
                    shadowFactor = mix(1.0, shadowFactor, clamp(params.shadowParams.w, 0.0, 1.0));
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
        float4 previous = historyTex.read(tid);
        current = mix(current, previous, historyWeight);
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
    if (depth >= 1.0) {
        return float4(current, 1.0);
    }
    float3 viewPos = reconstructViewPosition(in.uv, depth, camera);
    float distance = length(viewPos);

    float startDist = max(params.distanceParams.x, 0.0);
    float endDist = max(params.distanceParams.y, startDist + 0.001);
    float segmentStart = clamp(startDist, 0.0, distance);
    float segmentEnd = clamp(endDist, segmentStart, distance);
    if (segmentEnd <= segmentStart + 0.0001) {
        return float4(current, 1.0);
    }

    float nearPlane = max(params.volumeParams.x, 0.001);
    float farPlane = max(params.volumeParams.y, nearPlane + 0.001);
    float sliceCount = max(params.volumeParams.z, 1.0);
    float logDepth = log(farPlane / nearPlane);
    if (logDepth <= 0.0001) {
        return float4(current, 1.0);
    }

    float travel = segmentEnd - segmentStart;
    int steps = int(clamp(sliceCount * 0.5, 16.0, 64.0));
    float stepLength = travel / max(float(steps), 1.0);

    float3 accum = float3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= steps) {
            break;
        }
        float dist = segmentStart + (float(i) + 0.5) * stepLength;
        dist = max(dist, nearPlane);
        float slice = log(dist / nearPlane) / logDepth;
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
    depthcube_array<float> pointShadowCube0 [[texture(12)]],
    depthcube_array<float> pointShadowCube1 [[texture(13)]],
    depthcube_array<float> pointShadowCube2 [[texture(14)]],
    depthcube_array<float> pointShadowCube3 [[texture(15)]],
    texture2d<float> ormMap [[texture(16)]],
    texture2d<float> ssaoMap [[texture(17)]],
    texture2d<float> decalAlbedoMap [[texture(18)]],
    texture2d<float> decalNormalMap [[texture(19)]],
    texture2d<float> decalOrmMap [[texture(20)]],
    sampler textureSampler [[sampler(0)]],
    sampler environmentSampler [[sampler(1)]],
    sampler shadowSampler [[sampler(2)]]
) {
    float3 Vworld = normalize(camera.cameraPosition - in.worldPosition);
    float3 viewPos = (camera.viewMatrix * float4(in.worldPosition, 1.0)).xyz;
    float3 Vview = normalize(-viewPos);
    float3 T = normalize(in.tangent);
    float3 B = normalize(in.bitangent);
    float3 N = normalize(in.normal);
    float3x3 TBN = float3x3(T, B, N);
    float receiveShadows = material.heightParams.w;
    
    // View dir in tangent space for parallax
    float3 viewDirTS = normalize(TBN * Vworld);
    
    float2 uv = in.texCoord * material.uvTilingOffset.xy + material.uvTilingOffset.zw;
    
    // Parallax offset mapping (simple)
    if (material.textureFlags2.z > 0.5) {
        float numLayers = mix(material.heightParams.y, material.heightParams.z, 1.0 - abs(viewDirTS.z));
        float layerDepth = 1.0 / max(numLayers, 1.0);
        float currentLayerDepth = 0.0;
        float2 P = viewDirTS.xy * material.heightParams.x;
        float2 deltaUV = P / numLayers;
        float2 currentUV = uv;
        float currentHeight = 0.0;
        while (currentLayerDepth < 1.0) {
            currentHeight = heightMap.sample(textureSampler, currentUV).r;
            if (material.textureFlags2.w > 0.5) {
                currentHeight = 1.0 - currentHeight;
            }
            if (currentHeight < currentLayerDepth) {
                break;
            }
            currentUV -= deltaUV;
            currentLayerDepth += layerDepth;
        }
        uv = currentUV;
    }
    
    // Albedo
    float4 albedoSample = albedoMap.sample(textureSampler, uv);
    float3 albedo = material.albedo.rgb * in.color.rgb;
    if (material.textureFlags.x > 0.5) {
        albedo *= albedoSample.rgb;
    }
    float alpha = material.albedo.a * (material.textureFlags.x > 0.5 ? albedoSample.a : 1.0);
    
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
    
    if (material.textureFlags3.x > 0.5) {
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
    if (material.textureFlags.y > 0.5) {
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
    float3 V = normalize(camera.cameraPosition - in.worldPosition);
    
    // Calculate reflectance at normal incidence (dielectric vs metal)
    float3 F0 = float3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Reflectance equation
    float3 Lo = float3(0.0);
    
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
            
            float3 LdirWS = float3(0.0);
            float distance = 1.0;
            float attenuation = 1.0;
            
            if (type == 0) { // Directional
                LdirWS = normalize(-Ld.directionType.xyz); // view-space dir
            } else {
                float3 toLightVS = Ld.positionRange.xyz - viewPos;
                distance = length(toLightVS);
                if (distance < 1e-4) continue;
                LdirWS = normalize(toLightVS);
                float range = (Ld.positionRange.w > 0.0) ? (1.0 / Ld.positionRange.w) : 0.0;
                float smooth = pow(saturate(1.0 - pow(distance / max(range, 0.0001), 4.0)), 2.0);
                attenuation = smooth / max(distance * distance, 0.001);
                
                if (type == 2) { // Spot
                    float cosTheta = dot(normalize(Ld.directionType.xyz), -LdirWS);
                    if (cosTheta < Ld.misc.y) continue;
                    float spotAtten = smoothstep(Ld.misc.y, Ld.misc.x, cosTheta);
                    attenuation *= spotAtten;
                } else if (type == 3 || type == 4) { // Area approx
                    attenuation *= 1.0 / max(distance, 0.1);
                }
            }
            
            float NdotL = max(dot(Nview, LdirWS), 0.0);
            if (NdotL <= 0.0 || attenuation <= 0.0) {
                continue;
            }
            
            float3 H = normalize(Vview + LdirWS);
            float NDF = DistributionGGX(Nview, H, roughness);
            float G = GeometrySmith(Nview, Vview, LdirWS, roughness);
            float3 F = fresnelSchlick(max(dot(H, Vview), 0.0), F0);
            
            float3 numerator = NDF * G * F;
            float denom = 4.0 * max(dot(Nview, Vview), 0.0) * max(NdotL, 0.0) + 0.001;
            float3 specular = numerator / denom;
            
            float3 kS = F;
            float3 kD = (float3(1.0) - kS) * (1.0 - metallic);
            
            bool usePCSS = ((uint)Ld.shadowCookie.w & 1u) != 0;
            bool useContact = ((uint)Ld.shadowCookie.w & 2u) != 0;
            int shadowIdx = (int)round(Ld.shadowCookie.x);
            int cascadeCount = (type == 0 && Ld.shadowCookie.z >= 1.0) ? (int)round(Ld.shadowCookie.z) : 1;
            float3 LdirWorld = normalize((camera.viewMatrixInverse * float4(LdirWS, 0.0)).xyz);
            float shadow = 1.0;
            if (shadowIdx >= 0 && receiveShadows > 0.5) {
                if (type == 1) {
                    float3 lightPosWS = (camera.viewMatrixInverse * float4(Ld.positionRange.xyz, 1.0)).xyz;
                    ShadowGPUData sLocal = shadowData[shadowIdx];
                    shadow = samplePointShadow(shadowData, shadowIdx, in.worldPosition, N, lightPosWS, LdirWorld, sLocal.depthRange.w, pointShadowCube0, pointShadowCube1, pointShadowCube2, pointShadowCube3, shadowSampler);
                } else {
                    float viewDepth = max(-viewPos.z, 0.0);
                    shadow = sampleShadow(shadowData, shadowIdx, cascadeCount, viewDepth, in.worldPosition, N, LdirWorld, shadowAtlas, shadowSampler, usePCSS, useContact);
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
    float NdotV = max(dot(N, V), 0.0);
    
    // Check if we have proper IBL textures (environment.toneControl.y)
    bool hasProperIBL = environment.toneControl.y > 0.5;
    
    if (hasProperIBL) {
        // ========================================
        // PROPER IBL with precomputed maps
        // ========================================
        
        // Apply environment rotation to directions
        float3x3 envRot = environmentRotation(environment);
        float3 rotatedN = envRot * N;
        float3 R = reflect(-V, N);
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
        float3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);
        
        // Combine diffuse and specular IBL
        float3 diffuseIBL = kD * irradiance * albedo;
        float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        
        environmentLighting = (diffuseIBL + specularIBL) * ao;
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
        float3 R = reflect(-V, N);
        float roughLod = roughness * 6.0;
        float3 specularIBL = sampleEnvironment(environmentMap, environmentSampler, R, roughLod, environment);
        
        // Approximate BRDF integration
        float2 envBRDF = float2(1.0 - roughness, 0.0); // Rough approximation
        specularIBL *= (kS_ibl * envBRDF.x + envBRDF.y);
        
        environmentLighting = (kD_ibl * diffuseIBL + specularIBL) * ao;
    }
    
    environmentLighting *= environment.exposureIntensity.y; // IBL intensity
    
    // Add emission (unpack from Vector4)
    float3 emission = material.emission.xyz * material.emission.w; // emissionStrength in w
    if (material.textureFlags2.y > 0.5) {
        emission += emissionMap.sample(textureSampler, uv).rgb * material.emission.w;
    }
    
    // Final color - Direct + IBL + emission
    float3 color = Lo + environmentLighting + emission;
    
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
        float lod = max(environment.exposureIntensity.w, 1.5);
        color = sampleEnvironment(environmentMap, environmentSampler, worldDir, lod, environment);
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
    if (toneMapping > 0.5) {
        if (toneMapping < 1.5) {
            color = tonemapFilmic(color);
        } else {
            color = tonemapACES(color);
        }
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
        color += grain * grainIntensity;
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
