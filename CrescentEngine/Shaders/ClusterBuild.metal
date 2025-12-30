#include "Common.metal.h"
using namespace metal;

inline float3 unproject(float2 uv, float depth, float4x4 invProj) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 view = invProj * clip;
    return view.xyz / view.w;
}

kernel void cluster_build(const device LightGPUData* lights        [[buffer(0)]],
                          device ClusterHeader* clusterHeaders     [[buffer(1)]],
                          device uint* clusterIndices              [[buffer(2)]],
                          device atomic_uint* overflowCounter      [[buffer(3)]],
                          constant ClusterParams& params           [[buffer(4)]],
                          uint3 tid [[thread_position_in_grid]]) {
    if (tid.x >= params.clusterX || tid.y >= params.clusterY || tid.z >= params.clusterZ) {
        return;
    }
    uint clusterId = tid.z * (params.clusterX * params.clusterY) + tid.y * params.clusterX + tid.x;
    
    // Compute depth slice bounds (log distribution)
    float slice = float(tid.z);
    float sliceCount = float(params.clusterZ);
    float zNear = params.nearPlane * pow(params.farPlane / params.nearPlane, slice / sliceCount);
    float zFar  = params.nearPlane * pow(params.farPlane / params.nearPlane, (slice + 1.0) / sliceCount);
    
    // Screen tile bounds in NDC
    float2 ndcMin = float2(float(tid.x) / float(params.clusterX), float(tid.y) / float(params.clusterY));
    float2 ndcMax = float2(float(tid.x + 1) / float(params.clusterX), float(tid.y + 1) / float(params.clusterY));
    ndcMin = ndcMin * 2.0 - 1.0;
    ndcMax = ndcMax * 2.0 - 1.0;
    
    // Build conservative AABB in view space
    float3 corners[8];
    uint c = 0;
    for (int z = 0; z < 2; ++z) {
        float depth = (z == 0) ? (zNear / params.farPlane * 2.0 - 1.0) : (zFar / params.farPlane * 2.0 - 1.0);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                float2 uv = float2(x ? ndcMax.x : ndcMin.x, y ? ndcMax.y : ndcMin.y) * 0.5 + 0.5;
                corners[c++] = unproject(uv, depth, params.projectionInv);
            }
        }
    }
    float3 aabbMin = corners[0];
    float3 aabbMax = corners[0];
    for (uint i = 1; i < 8; ++i) {
        aabbMin = min(aabbMin, corners[i]);
        aabbMax = max(aabbMax, corners[i]);
    }
    
    // Offset for this cluster in index buffer
    uint base = clusterId * params.maxLightsPerCluster;
    uint count = 0;
    
    // Cull lights
    for (uint i = 0; i < params.lightCount; ++i) {
        LightGPUData L = lights[i];
        int type = (int)round(L.directionType.w);
        if (type == 0) {
            // Directional affects all clusters
        } else {
            float3 posVS = L.positionRange.xyz;
            float range = (L.positionRange.w > 0.0f) ? 1.0f / L.positionRange.w : 0.0f;
            float radius = range;
            // Sphere-AABB test
            float3 closest = clamp(posVS, aabbMin, aabbMax);
            float3 d = posVS - closest;
            float dist2 = dot(d, d);
            if (dist2 > radius * radius) {
                continue;
            }
        }
        
        if (count < params.maxLightsPerCluster) {
            clusterIndices[base + count] = i;
            ++count;
        } else {
            atomic_fetch_add_explicit(overflowCounter, 1, memory_order_relaxed);
        }
    }
    
    clusterHeaders[clusterId].offset = base;
    clusterHeaders[clusterId].count = count;
}
