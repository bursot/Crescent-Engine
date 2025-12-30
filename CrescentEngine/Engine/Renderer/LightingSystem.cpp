#include "LightingSystem.hpp"
#include "../Scene/Scene.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Components/Camera.hpp"
#include "DebugRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Crescent {

ShadowAtlas::ShadowAtlas(uint32_t resolution, uint32_t layers)
    : m_resolution(resolution)
    , m_layers(layers)
    , m_cursorX(0)
    , m_cursorY(0)
    , m_rowHeight(0) {
    reset();
}

void ShadowAtlas::reset() {
    m_cursorX = 0;
    m_cursorY = 0;
    m_rowHeight = 0;
    m_tiles.clear();
}

ShadowAtlasTile ShadowAtlas::allocate(uint32_t size) {
    ShadowAtlasTile tile{};
    if (size == 0) {
        return tile;
    }
    
    size = std::min(size, m_resolution);
    
    // Simple skyline row allocation
    if (m_cursorX + size > m_resolution) {
        m_cursorX = 0;
        m_cursorY += m_rowHeight;
        m_rowHeight = 0;
    }
    
    if (m_cursorY + size > m_resolution) {
        return tile; // invalid
    }
    
    tile.valid = true;
    tile.x = m_cursorX;
    tile.y = m_cursorY;
    tile.size = size;
    tile.layer = 0;
    
    m_tiles.push_back(tile);
    
    m_cursorX += size;
    m_rowHeight = std::max(m_rowHeight, size);
    
    return tile;
}

LightingSystem::LightingSystem()
    : m_shadowAtlas(4096, 1)
    , m_viewportWidth(1)
    , m_viewportHeight(1)
    , m_debugDrawAtlas(false)
    , m_pointCubeCounts({0,0,0,0}) {
}

void LightingSystem::configureShadowAtlas(uint32_t resolution, uint32_t layers) {
    m_shadowAtlas = ShadowAtlas(resolution, layers);
}

void LightingSystem::beginFrame(Scene* scene, Camera* camera, uint32_t viewportWidth, uint32_t viewportHeight) {
    m_viewportWidth = viewportWidth;
    m_viewportHeight = viewportHeight;
    m_preparedLights.clear();
    m_gpuLights.clear();
    m_gpuShadows.clear();
    m_cascades.clear();
    m_shadowAtlas.reset();
    m_pointCubeCounts = {0,0,0,0};
    
    if (!scene || !camera) {
        return;
    }
    
    gatherLights(scene, camera);
    allocateShadows();
    fillGPUBuffers();
}

void LightingSystem::gatherLights(Scene* scene, Camera* camera) {
    if (!scene || !camera) {
        return;
    }
    
    const auto& entities = scene->getAllEntities();
    Math::Matrix4x4 view = camera->getViewMatrix();
    
    for (const auto& entityPtr : entities) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy()) {
            continue;
        }
        
        Light* light = entity->getComponent<Light>();
        if (!light) {
            continue;
        }
        
        Transform* transform = entity->getTransform();
        PreparedLight prepared;
        prepared.light = light;
        prepared.positionWS = transform->getPosition();
        prepared.directionWS = transform->forward();
        prepared.positionVS = view.transformPoint(prepared.positionWS);
        prepared.directionVS = view.transformDirection(prepared.directionWS).normalized();
        prepared.range = light->getRange();
        prepared.shadowStart = UINT32_MAX;
        prepared.shadowCount = 0;
        
        m_preparedLights.push_back(prepared);
        
        if (light->getCastShadows() && light->getType() == Light::Type::Directional) {
            buildDirectionalCascades(prepared, camera);
        }
    }
}

void LightingSystem::buildDirectionalCascades(const PreparedLight& light, Camera* camera) {
    if (!camera || !light.light) {
        return;
    }
    
    const uint32_t lightResolution = light.light->getShadowMapResolution();
    const float camNear = camera->getNearClip();
    const float camFar = camera->getFarClip();
    const float shadowNear = std::max(camNear, light.light->getShadowNearPlane());
    const float shadowFar = std::min(camFar, light.light->getShadowFarPlane());
    if (shadowFar <= shadowNear + 0.001f) {
        return;
    }
    
    const auto splits = light.light->getCascadeSplits();
    const uint8_t cascadeCount = std::max<uint8_t>(1, std::min<uint8_t>(light.light->getCascadeCount(), 4));
    
    Math::Vector3 lightDir = light.directionWS.normalized();
    
    float prevSplit = 0.0f;
    for (uint8_t cascadeIdx = 0; cascadeIdx < cascadeCount; ++cascadeIdx) {
        float split = splits[cascadeIdx];
        float cascadeNear = Math::Lerp(shadowNear, shadowFar, prevSplit);
        float cascadeFar = Math::Lerp(shadowNear, shadowFar, split);
        
        auto frustumCorners = computeFrustumCornersWS(camera, cascadeNear, cascadeFar);

        Math::Vector3 center(0.0f, 0.0f, 0.0f);
        for (const auto& corner : frustumCorners) {
            center += corner;
        }
        center /= static_cast<float>(frustumCorners.size());

        float radius = 0.0f;
        for (const auto& corner : frustumCorners) {
            radius = std::max(radius, (corner - center).length());
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        Math::Vector3 up = Math::Vector3::Up;
        if (std::abs(lightDir.dot(up)) > 0.99f) {
            up = Math::Vector3::Right;
        }

        uint32_t cascadeRes = lightResolution;
        if (cascadeIdx == 0 && lightResolution <= 1024) {
            cascadeRes = lightResolution * 2;
        }
        cascadeRes = std::min<uint32_t>(cascadeRes, m_shadowAtlas.getResolution());

        float lightDistance = radius + 10.0f;
        Math::Vector3 lightPos = center - lightDir * lightDistance;
        Math::Matrix4x4 view = Math::Matrix4x4::LookAt(lightPos, center, up);

        float texelSize = (radius * 2.0f) / static_cast<float>(cascadeRes);
        Math::Vector3 centerLS = view.transformPoint(center);
        centerLS.x = std::floor(centerLS.x / texelSize) * texelSize;
        centerLS.y = std::floor(centerLS.y / texelSize) * texelSize;
        centerLS.z = std::floor(centerLS.z / texelSize) * texelSize;
        Math::Vector3 snappedCenter = view.inversed().transformPoint(centerLS);
        lightPos = snappedCenter - lightDir * lightDistance;
        view = Math::Matrix4x4::LookAt(lightPos, snappedCenter, up);

        float lightNear = 0.1f;
        float lightFar = lightDistance + radius;
        Math::Matrix4x4 proj = Math::Matrix4x4::Orthographic(-radius, radius, -radius, radius, lightNear, lightFar);
        
        CascadedSlice slice;
        slice.view = view;
        slice.proj = proj;
        slice.viewProj = proj * view;
        slice.resolution = cascadeRes;
        slice.texelWorldSize = (radius * 2.0f) / static_cast<float>(cascadeRes);
        slice.splitNear = cascadeNear;
        slice.splitFar = cascadeFar;
        m_cascades.push_back(slice);
        
        prevSplit = split;
    }
}

void LightingSystem::allocateShadows() {
    const Light* primaryDirectional = nullptr;
    for (const auto& prepared : m_preparedLights) {
        if (prepared.light && prepared.light->getType() == Light::Type::Directional && prepared.light->getCastShadows()) {
            primaryDirectional = prepared.light;
            break;
        }
    }
    const float cascadeBias = primaryDirectional ? primaryDirectional->getShadowBias() : 0.0005f;
    const float cascadeNormalBias = primaryDirectional ? primaryDirectional->getShadowNormalBias() : 0.001f;
    const float cascadePenumbra = primaryDirectional ? primaryDirectional->getPenumbra() : 1.0f;
    const float biasScale = 0.1f;
    const float normalBiasScale = 0.5f;

    // Directional cascades
    uint32_t shadowIdx = 0;
    for (auto& slice : m_cascades) {
        uint32_t res = std::max(256u, std::min<uint32_t>(8192u, slice.resolution));
        slice.atlas = m_shadowAtlas.allocate(res);
        if (slice.atlas.valid) {
            ++shadowIdx;
        }
    }
    
    // Per-light shadows
    for (auto& prepared : m_preparedLights) {
        Light* light = prepared.light;
        if (!light || !light->getCastShadows()) {
            continue;
        }
        
        // Directional lights use cascades, handled separately below
        if (light->getType() == Light::Type::Directional) {
            continue;
        }
        
        Math::Vector3 pos = prepared.positionWS;
        Math::Vector3 dir = prepared.directionWS.normalized();
        Math::Vector3 up = Math::Vector3::Up;
        if (std::abs(prepared.directionWS.dot(up)) > 0.99f) {
            up = Math::Vector3::Right;
        }
        
        if (light->getType() == Light::Type::Point) {
            // Bucket to resolution tier
            const uint32_t presets[4] = {256, 512, 1024, 2048};
            uint32_t reqRes = light->getShadowMapResolution();
            int tier = 0;
            for (int t = 0; t < 4; ++t) {
                tier = t;
                if (reqRes <= presets[t]) break;
            }
            uint32_t res = presets[tier];
            
            prepared.shadowStart = static_cast<uint32_t>(m_gpuShadows.size());
            prepared.shadowCount = 1;
            
            ShadowGPUData gpuShadow{};
            gpuShadow.params = Math::Vector4(light->getShadowBias(), light->getShadowNormalBias(), light->getPenumbra(), 3.0f); // type=point cube
            float nearPlane = std::max(0.01f, light->getRange() * 0.05f);
            nearPlane = std::min(nearPlane, light->getRange() * 0.5f);
            gpuShadow.depthRange = Math::Vector4(nearPlane, light->getRange(), static_cast<float>(m_pointCubeCounts[tier]), (float)tier); // cube index in z, tier in w
            gpuShadow.atlasUV = Math::Vector4((float)res, 0.0f, 0.0f, 0.0f); // store resolution
            m_gpuShadows.push_back(gpuShadow);
            m_pointCubeCounts[tier]++;
        } else {
            const uint32_t resolution = light->getShadowMapResolution();
            ShadowAtlasTile tile = m_shadowAtlas.allocate(resolution);
            if (!tile.valid) {
                continue;
            }
            
            prepared.shadowStart = static_cast<uint32_t>(m_gpuShadows.size());
            prepared.shadowCount = 1;
            
            ShadowGPUData gpuShadow{};
            Math::Vector3 up = Math::Vector3::Up;
            if (std::abs(light->getDirection().dot(up)) > 0.99f) {
                up = Math::Vector3::Right;
            }
            
            Math::Vector3 pos = prepared.positionWS;
            Math::Vector3 dir = prepared.directionWS.normalized();
            
            Math::Matrix4x4 view;
            Math::Matrix4x4 proj;
            
            float texelWorld = 0.0f;
            if (light->getType() == Light::Type::Spot) {
                view = Math::Matrix4x4::LookAt(pos, pos + dir * light->getRange(), up);
                // Use full spot angle as FOV, add small margin to avoid edge artifacts
                float spotAngleRad = light->getSpotAngle() * Math::DEG_TO_RAD;
                float fov = spotAngleRad * 1.1f; // 10% margin
                fov = std::min(fov, Math::PI * 0.95f); // Cap at ~171 degrees
                float aspect = 1.0f;
                proj = Math::Matrix4x4::Perspective(fov, aspect, 0.01f, light->getRange());
                gpuShadow.params.w = 0.0f; // spot identifier
                float extent = std::tan(fov * 0.5f) * light->getRange();
                texelWorld = (extent * 2.0f) / static_cast<float>(resolution);
            } else {
                view = Math::Matrix4x4::LookAt(pos, pos + dir, up);
                float extent = light->getRange();
                proj = Math::Matrix4x4::Orthographic(-extent, extent, -extent, extent, 0.01f, extent * 2.0f);
                gpuShadow.params.w = 1.0f; // area/point identifier
                texelWorld = (extent * 2.0f) / static_cast<float>(resolution);
            }
            
            gpuShadow.viewProj = proj * view;
            gpuShadow.atlasUV = Math::Vector4(
                static_cast<float>(tile.x) / static_cast<float>(m_shadowAtlas.getResolution()),
                static_cast<float>(tile.y) / static_cast<float>(m_shadowAtlas.getResolution()),
                static_cast<float>(tile.size) / static_cast<float>(m_shadowAtlas.getResolution()),
                static_cast<float>(tile.size) / static_cast<float>(m_shadowAtlas.getResolution())
            );
            
            float bias = std::max(light->getShadowBias(), texelWorld * biasScale);
            float normalBias = std::max(light->getShadowNormalBias(), texelWorld * normalBiasScale);
            gpuShadow.params.x = bias;
            gpuShadow.params.y = normalBias;
            gpuShadow.params.z = light->getPenumbra();
            gpuShadow.depthRange = Math::Vector4(light->getShadowNearPlane(), light->getShadowFarPlane(), texelWorld, static_cast<float>(tile.layer));
            
            m_gpuShadows.push_back(gpuShadow);
        }
    }
    
    // Append cascade shadow entries after atlas allocation, preserve indices
    uint32_t cascadeBase = static_cast<uint32_t>(m_gpuShadows.size());
    for (size_t i = 0; i < m_cascades.size(); ++i) {
        const auto& slice = m_cascades[i];
        if (!slice.atlas.valid) {
            continue;
        }
        
        ShadowGPUData gpuShadow{};
        gpuShadow.viewProj = slice.viewProj;
        gpuShadow.atlasUV = Math::Vector4(
            static_cast<float>(slice.atlas.x) / static_cast<float>(m_shadowAtlas.getResolution()),
            static_cast<float>(slice.atlas.y) / static_cast<float>(m_shadowAtlas.getResolution()),
            static_cast<float>(slice.atlas.size) / static_cast<float>(m_shadowAtlas.getResolution()),
            static_cast<float>(slice.atlas.size) / static_cast<float>(m_shadowAtlas.getResolution())
        );
        float texelWorld = slice.texelWorldSize;
        float bias = std::max(cascadeBias, texelWorld * biasScale);
        float normalBias = std::max(cascadeNormalBias, texelWorld * normalBiasScale);
        gpuShadow.params = Math::Vector4(bias, normalBias, cascadePenumbra, static_cast<float>(i)); // bias/normalBias/penumbra/cascadeId
        gpuShadow.depthRange = Math::Vector4(slice.splitNear, slice.splitFar, texelWorld, static_cast<float>(slice.atlas.layer));
        
        m_gpuShadows.push_back(gpuShadow);
    }
    
    // Map cascades to directional lights (assumes primary directional uses cascades 0..cascadeCount-1)
    for (auto& prepared : m_preparedLights) {
        if (!prepared.light || prepared.light->getType() != Light::Type::Directional || !prepared.light->getCastShadows()) {
            continue;
        }
        prepared.shadowStart = cascadeBase;
        prepared.shadowCount = static_cast<uint32_t>(m_cascades.size());
    }
}

void LightingSystem::fillGPUBuffers() {
    for (const auto& prepared : m_preparedLights) {
        if (!prepared.light) {
            continue;
        }
        
        LightGPUData gpu{};
        float invRange = (prepared.range > Math::EPSILON) ? (1.0f / prepared.range) : 0.0f;
        gpu.positionRange = Math::Vector4(prepared.positionVS.x, prepared.positionVS.y, prepared.positionVS.z, invRange);
        gpu.directionType = Math::Vector4(prepared.directionVS.x, prepared.directionVS.y, prepared.directionVS.z, static_cast<float>(prepared.light->getType()));
        Math::Vector3 color = prepared.light->getEffectiveColor();
        
        // Physically plausible intensity handling
        float intensity = prepared.light->getIntensity();
        switch (prepared.light->getType()) {
            case Light::Type::Directional: {
                // Treat as illuminance (lux) at surface
                gpu.colorIntensity = Math::Vector4(color.x, color.y, color.z, intensity);
                break;
            }
            case Light::Type::Point: {
                // Lumens -> candela
                float cd = intensity / (4.0f * Math::PI);
                gpu.colorIntensity = Math::Vector4(color.x, color.y, color.z, cd);
                break;
            }
            case Light::Type::Spot: {
                float solidAngle = 2.0f * Math::PI * (1.0f - std::cos(prepared.light->getSpotAngle() * 0.5f * Math::DEG_TO_RAD));
                float cd = (solidAngle > Math::EPSILON) ? intensity / solidAngle : intensity;
                gpu.colorIntensity = Math::Vector4(color.x, color.y, color.z, cd);
                break;
            }
            case Light::Type::AreaRect:
            case Light::Type::AreaDisk: {
                // Treat as luminance (nits)
                gpu.colorIntensity = Math::Vector4(color.x, color.y, color.z, intensity);
                break;
            }
            default:
                gpu.colorIntensity = Math::Vector4(color.x, color.y, color.z, intensity);
                break;
        }
        
        float innerCos = std::cos(prepared.light->getInnerSpotAngle() * Math::DEG_TO_RAD * 0.5f);
        float outerCos = std::cos(prepared.light->getSpotAngle() * Math::DEG_TO_RAD * 0.5f);
        Math::Vector2 areaSize = prepared.light->getAreaSize();
        gpu.misc = Math::Vector4(innerCos, outerCos, areaSize.x, areaSize.y);
        
        uint32_t flags = 0;
        if (prepared.light->getSoftShadows()) flags |= 1u;
        if (prepared.light->getContactShadows()) flags |= 2u;
        if (prepared.light->getVolumetric()) flags |= 4u;
        
        float shadowIdx = prepared.shadowStart == UINT32_MAX ? -1.0f : static_cast<float>(prepared.shadowStart);
        float shadowCount = static_cast<float>(prepared.shadowCount);
        gpu.shadowCookie = Math::Vector4(
            shadowIdx,
            static_cast<float>(prepared.light->getCookieIndex()),
            (prepared.light->getType() == Light::Type::Directional || prepared.light->getType() == Light::Type::Point)
                ? shadowCount
                : static_cast<float>(prepared.light->getIESProfileIndex()),
            static_cast<float>(flags)
        );
        
        m_gpuLights.push_back(gpu);
    }
}

std::array<Math::Vector3, 8> LightingSystem::computeFrustumCornersWS(Camera* camera, float nearPlane, float farPlane) const {
    std::array<Math::Vector3, 8> corners;
    
    Transform* camTransform = camera->getEntity()->getTransform();
    const Math::Vector3 camPos = camTransform->getPosition();
    const Math::Vector3 forward = camTransform->forward();
    const Math::Vector3 right = camTransform->right();
    const Math::Vector3 up = camTransform->up();
    
    float fov = camera->getFieldOfView();
    float aspect = camera->getAspectRatio();
    float tanHalfFov = std::tan(fov * 0.5f);
    
    float nearHeight = nearPlane * tanHalfFov;
    float nearWidth = nearHeight * aspect;
    float farHeight = farPlane * tanHalfFov;
    float farWidth = farHeight * aspect;
    
    Math::Vector3 nearCenter = camPos + forward * nearPlane;
    Math::Vector3 farCenter = camPos + forward * farPlane;
    
    // Near plane
    corners[0] = nearCenter + up * nearHeight - right * nearWidth; // ntl
    corners[1] = nearCenter + up * nearHeight + right * nearWidth; // ntr
    corners[2] = nearCenter - up * nearHeight - right * nearWidth; // nbl
    corners[3] = nearCenter - up * nearHeight + right * nearWidth; // nbr
    
    // Far plane
    corners[4] = farCenter + up * farHeight - right * farWidth; // ftl
    corners[5] = farCenter + up * farHeight + right * farWidth; // ftr
    corners[6] = farCenter - up * farHeight - right * farWidth; // fbl
    corners[7] = farCenter - up * farHeight + right * farWidth; // fbr
    
    return corners;
}

void LightingSystem::buildLightGizmos(DebugRenderer& debug, bool drawCascades) const {
    for (const auto& light : m_preparedLights) {
        if (!light.light) continue;
        
        Math::Vector4 gizmoColor(1.0f, 0.9f, 0.5f, 1.0f);
        switch (light.light->getType()) {
            case Light::Type::Directional: {
                // Arrow
                debug.drawRay(light.positionWS, light.directionWS, 4.0f, Math::Vector4(1.0f, 0.95f, 0.6f, 1.0f));
                break;
            }
            case Light::Type::Point: {
                debug.drawSphere(light.positionWS, light.range, gizmoColor);
                break;
            }
            case Light::Type::Spot: {
                drawSpotGizmo(debug, light);
                break;
            }
            case Light::Type::AreaRect:
            case Light::Type::AreaDisk: {
                drawAreaGizmo(debug, light);
                break;
            }
            case Light::Type::EmissiveMesh: {
                debug.drawSphere(light.positionWS, 0.2f, Math::Vector4(0.8f, 0.3f, 1.0f, 1.0f));
                break;
            }
        }
    }
    
    if (drawCascades) {
        // Draw cascade boxes
        for (const auto& cascade : m_cascades) {
            Math::Matrix4x4 inv = cascade.viewProj.inversed();
            std::array<Math::Vector3, 8> corners{};
            int idx = 0;
            for (int z = 0; z <= 1; ++z) {
                for (int y = 0; y <= 1; ++y) {
                    for (int x = 0; x <= 1; ++x) {
                        Math::Vector4 clip((float)x * 2.0f - 1.0f, (float)y * 2.0f - 1.0f, (float)z * 2.0f - 1.0f, 1.0f);
                        Math::Vector4 world = inv * clip;
                        world.x /= world.w;
                        world.y /= world.w;
                        world.z /= world.w;
                        corners[idx++] = Math::Vector3(world.x, world.y, world.z);
                    }
                }
            }
            
            // Draw edges of the frustum box
            const int edges[12][2] = {
                {0,1},{1,3},{3,2},{2,0}, // near
                {4,5},{5,7},{7,6},{6,4}, // far
                {0,4},{1,5},{2,6},{3,7}  // connect
            };
            for (auto& edge : edges) {
                debug.drawLine(corners[edge[0]], corners[edge[1]], Math::Vector4(0.2f, 0.6f, 1.0f, 1.0f));
            }
        }
    }
    
    if (m_debugDrawAtlas) {
        // Draw atlas occupancy rectangles in normalized space
        float res = static_cast<float>(m_shadowAtlas.getResolution());
        for (const auto& tile : m_shadowAtlas.getTiles()) {
            if (!tile.valid) continue;
            Math::Vector2 min(static_cast<float>(tile.x) / res, static_cast<float>(tile.y) / res);
            Math::Vector2 max(static_cast<float>(tile.x + tile.size) / res, static_cast<float>(tile.y + tile.size) / res);
            Math::Vector3 origin(min.x * 10.0f, 0.0f, min.y * 10.0f);
            Math::Vector3 size((max.x - min.x) * 10.0f, 0.0f, (max.y - min.y) * 10.0f);
            debug.drawBox(origin + size * 0.5f, size, Math::Vector4(0.8f, 0.2f, 0.9f, 1.0f));
        }
    }
}

void LightingSystem::drawSpotGizmo(DebugRenderer& debug, const PreparedLight& light) const {
    const float outer = light.light->getSpotAngle() * Math::DEG_TO_RAD * 0.5f;
    float radius = std::tan(outer) * light.range;
    Math::Vector3 tip = light.positionWS;
    // Light shines in the direction of forward vector (directionWS)
    Math::Vector3 dir = light.directionWS.normalized();
    Math::Vector3 up = Math::Vector3::Up;
    if (std::abs(dir.dot(up)) > 0.99f) {
        up = Math::Vector3::Right;
    }
    Math::Vector3 right = dir.cross(up).normalized();
    up = right.cross(dir).normalized();
    
    // Cone base is at tip + dir * range (in the direction light is shining)
    Math::Vector3 baseCenter = tip + dir * light.range;
    const int segments = 24;
    std::vector<Math::Vector3> ring;
    ring.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        float theta = (Math::TWO_PI * i) / static_cast<float>(segments);
        Math::Vector3 p = baseCenter + (std::cos(theta) * right + std::sin(theta) * up) * radius;
        ring.push_back(p);
        debug.drawLine(tip, p, Math::Vector4(1.0f, 0.8f, 0.2f, 1.0f));
    }
    for (int i = 0; i < segments; ++i) {
        debug.drawLine(ring[i], ring[(i + 1) % segments], Math::Vector4(1.0f, 0.8f, 0.2f, 1.0f));
    }
}

void LightingSystem::drawAreaGizmo(DebugRenderer& debug, const PreparedLight& light) const {
    Math::Vector3 pos = light.positionWS;
    Math::Vector3 dir = light.directionWS.normalized();
    Math::Vector3 up = Math::Vector3::Up;
    if (std::abs(dir.dot(up)) > 0.99f) {
        up = Math::Vector3::Right;
    }
    Math::Vector3 right = dir.cross(up).normalized();
    up = right.cross(dir).normalized();
    
    Math::Vector2 size = light.light->getAreaSize() * 0.5f;
    Math::Vector3 center = pos;
    
    if (light.light->getType() == Light::Type::AreaRect) {
        Math::Vector3 corners[4] = {
            center - right * size.x - up * size.y,
            center + right * size.x - up * size.y,
            center + right * size.x + up * size.y,
            center - right * size.x + up * size.y
        };
        for (int i = 0; i < 4; ++i) {
            debug.drawLine(corners[i], corners[(i + 1) % 4], Math::Vector4(0.5f, 0.9f, 1.0f, 1.0f));
        }
    } else {
        float radius = size.x;
        const int segments = 24;
        Math::Vector3 prev;
        for (int i = 0; i <= segments; ++i) {
            float theta = (Math::TWO_PI * i) / static_cast<float>(segments);
            Math::Vector3 p = center + (std::cos(theta) * right + std::sin(theta) * up) * radius;
            if (i > 0) {
                debug.drawLine(prev, p, Math::Vector4(0.5f, 0.9f, 1.0f, 1.0f));
            }
            prev = p;
        }
    }
    
    debug.drawRay(center, dir, light.light->getRange(), Math::Vector4(0.4f, 0.8f, 1.0f, 1.0f));
}

} // namespace Crescent
