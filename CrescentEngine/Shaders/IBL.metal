#include <metal_stdlib>
#include "Common.metal.h"
using namespace metal;

// =============================================================================
// COMMON UTILITIES
// =============================================================================



// Hammersley sequence for quasi-random sampling
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 hammersley(uint i, uint N) {
    return float2(float(i) / float(N), radicalInverse_VdC(i));
}

// GGX importance sampling
float3 importanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
    
    float phi = TWO_PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // Spherical to cartesian
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // Tangent-space to world-space
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// Direction to cubemap face and UV
float3 getCubeDirection(uint face, float2 uv) {
    // uv is in [0,1], convert to [-1,1]
    float2 st = uv * 2.0 - 1.0;
    
    float3 dir;
    switch (face) {
        case 0: dir = float3( 1.0, -st.y, -st.x); break; // +X
        case 1: dir = float3(-1.0, -st.y,  st.x); break; // -X
        case 2: dir = float3( st.x,  1.0,  st.y); break; // +Y
        case 3: dir = float3( st.x, -1.0, -st.y); break; // -Y
        case 4: dir = float3( st.x, -st.y,  1.0); break; // +Z
        case 5: dir = float3(-st.x, -st.y, -1.0); break; // -Z
    }
    return normalize(dir);
}



// =============================================================================
// BRDF LUT GENERATION
// Precomputes F0 scale and bias for split-sum approximation
// =============================================================================

float geometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float2 integrateBRDF(float NdotV, float roughness) {
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;
    
    float A = 0.0;
    float B = 0.0;
    
    float3 N = float3(0.0, 0.0, 1.0);
    
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        float2 Xi = hammersley(i, SAMPLE_COUNT);
        float3 H = importanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        
        if (NdotL > 0.0) {
            float G = geometrySmith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

kernel void brdfLUTKernel(
    texture2d<float, access::write> output [[texture(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    float2 texSize = float2(output.get_width(), output.get_height());
    if (gid.x >= uint(texSize.x) || gid.y >= uint(texSize.y)) return;
    
    float NdotV = (float(gid.x) + 0.5) / texSize.x;
    float roughness = (float(gid.y) + 0.5) / texSize.y;
    
    // Avoid edge cases
    NdotV = max(NdotV, 0.001);
    roughness = max(roughness, 0.001);
    
    float2 brdf = integrateBRDF(NdotV, roughness);
    output.write(float4(brdf, 0.0, 1.0), gid);
}

// =============================================================================
// EQUIRECTANGULAR TO CUBEMAP CONVERSION
// =============================================================================

kernel void equirectToCubeKernel(
    texture2d<float, access::sample> equirect [[texture(0)]],
    texturecube<float, access::write> cubemap [[texture(1)]],
    sampler linearSampler [[sampler(0)]],
    uint3 gid [[thread_position_in_grid]]
) {
    uint face = gid.z;
    uint size = cubemap.get_width();
    if (gid.x >= size || gid.y >= size || face >= 6) return;
    
    float2 uv = (float2(gid.xy) + 0.5) / float(size);
    float3 dir = getCubeDirection(face, uv);
    
    float2 equirectUV = directionToEquirectUV(dir);
    float4 color = equirect.sample(linearSampler, equirectUV);
    
    cubemap.write(color, gid.xy, face);
}

// =============================================================================
// PREFILTERED ENVIRONMENT MAP (Specular IBL)
// =============================================================================

struct PrefilteredParams {
    float roughness;
    uint resolution;
    uint sampleCount;
    uint face;
};

kernel void prefilteredEnvKernel(
    texturecube<float, access::sample> envMap [[texture(0)]],
    texturecube<float, access::write> prefilteredMap [[texture(1)]],
    sampler linearSampler [[sampler(0)]],
    constant PrefilteredParams& params [[buffer(0)]],
    uint3 gid [[thread_position_in_grid]]
) {
    uint face = params.face;
    uint size = params.resolution;
    if (gid.x >= size || gid.y >= size) return;
    
    float2 uv = (float2(gid.xy) + 0.5) / float(size);
    float3 N = getCubeDirection(face, uv);
    float3 R = N;
    float3 V = R;
    
    float3 prefilteredColor = float3(0.0);
    float totalWeight = 0.0;
    
    float roughness = max(params.roughness, 0.001); // Avoid division by zero
    uint sampleCount = params.sampleCount;
    
    // Environment map resolution for mipmap calculation
    float envMapSize = float(envMap.get_width());
    
    for (uint i = 0u; i < sampleCount; ++i) {
        float2 Xi = hammersley(i, sampleCount);
        float3 H = importanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);
        
        if (NdotL > 0.0) {
            // Calculate PDF for importance sampling (GGX distribution)
            float a = roughness * roughness;
            float a2 = a * a;
            float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
            float D = a2 / (PI * denom * denom);
            float pdf = (D * NdotH / (4.0 * HdotV)) + 0.0001;
            
            // Calculate solid angle of texel and sample
            float saTexel = 4.0 * PI / (6.0 * envMapSize * envMapSize);
            float saSample = 1.0 / (float(sampleCount) * pdf + 0.0001);
            
            // Mipmap level based on roughness and solid angle ratio
            // This prevents aliasing from bright sun spots
            float mipLevel = (roughness == 0.0) ? 0.0 : 0.5 * log2(saSample / saTexel);
            mipLevel = max(mipLevel, 0.0);
            
            // Sample environment map with calculated mip level
            float3 sampleColor = envMap.sample(linearSampler, L, level(mipLevel)).rgb;
            prefilteredColor += sampleColor * NdotL;
            totalWeight += NdotL;
        }
    }
    
    prefilteredColor = prefilteredColor / max(totalWeight, 0.001);
    prefilteredMap.write(float4(prefilteredColor, 1.0), gid.xy, face);
}

// =============================================================================
// IRRADIANCE MAP (Diffuse IBL)
// =============================================================================

kernel void irradianceKernel(
    texturecube<float, access::sample> envMap [[texture(0)]],
    texturecube<float, access::write> irradianceMap [[texture(1)]],
    sampler linearSampler [[sampler(0)]],
    constant uint& face [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    uint size = irradianceMap.get_width();
    if (gid.x >= size || gid.y >= size) return;
    
    float2 uv = (float2(gid.xy) + 0.5) / float(size);
    float3 N = getCubeDirection(face, uv);
    
    float3 irradiance = float3(0.0);
    
    // Create tangent space
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);
    
    float sampleDelta = 0.025;
    float nrSamples = 0.0;
    
    for (float phi = 0.0; phi < TWO_PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < HALF_PI; theta += sampleDelta) {
            // Spherical to cartesian (in tangent space)
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            
            // Tangent space to world
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
            
            irradiance += envMap.sample(linearSampler, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }
    
    irradiance = PI * irradiance / max(nrSamples, 1.0);
    irradianceMap.write(float4(irradiance, 1.0), gid.xy, face);
}
