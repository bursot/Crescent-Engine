#ifndef COMMON_METAL_H
#define COMMON_METAL_H

#include <metal_stdlib>
using namespace metal;

// ============================================================================
// SHARED CONSTANTS (MACRO — linker-safe)
// ============================================================================

#define PI       3.14159265359
#define TWO_PI   6.28318530718
#define HALF_PI  1.57079632679

// ============================================================================
// SHARED HELPER FUNCTIONS (STATIC INLINE ONLY)
// ============================================================================

static inline float2 directionToEquirectUV(float3 dir) {
    float2 uv;
    uv.x = atan2(dir.z, dir.x) / TWO_PI + 0.5;
    uv.y = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    return uv;
}

// ============================================================================
// SHARED UNIFORM STRUCTURES
// ============================================================================

struct ModelUniforms {
    float4x4 modelMatrix;
    float4x4 normalMatrix;
};

struct CameraUniforms {
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4x4 projectionMatrixNoJitter;
    float4x4 projectionMatrixNoJitterInverse;
    float4x4 viewProjectionMatrix;
    float4x4 viewMatrixInverse;
    float4x4 projectionMatrixInverse;
    float3 cameraPosition;
};

struct MaterialUniforms {
    float4 albedo;
    float4 properties;
    float4 emission;
    float4 uvTilingOffset;
    float4 textureFlags;
    float4 textureFlags2;
    float4 textureFlags3;
    float4 heightParams;
};

struct LightData {
    float4 direction;
    float4 color;
};

struct EnvironmentUniforms {
    float4 exposureIntensity;
    float4 colorControl;
    float4 toneControl;
    float4 skyParams;
    float4 rot0;
    float4 rot1;
    float4 rot2;
};

struct LightGPUData {
    float4 positionRange;   // xyz view-space position, w invRange
    float4 directionType;   // xyz view-space dir, w type
    float4 colorIntensity;  // rgb color, w intensity
    float4 misc;            // x cosInner, y cosOuter, z areaX, w areaY
    float4 shadowCookie;    // x shadowIdx, y cookieIdx, z iesIdx, w flags
};

struct ShadowGPUData {
    float4x4 viewProj;
    float4 atlasUV;   // xy offset, zw scale in atlas UV space
    float4 params;    // x bias, y normalBias, z penumbra, w type/cascade idx
    float4 depthRange; // x near, y far, z padding, w atlas layer
};

struct ClusterHeader {
    uint offset;
    uint count;
    uint _pad0;
    uint _pad1;
};

// Matches ClusteredLightingPass::Params layout
struct ClusterParams {
    float4x4 projection;
    float4x4 projectionInv;
    uint clusterX;
    uint clusterY;
    uint clusterZ;
    uint lightCount;
    uint maxLightsPerCluster;
    float screenWidth;
    float screenHeight;
    float nearPlane;
    float farPlane;
};

#endif // COMMON_METAL_H
