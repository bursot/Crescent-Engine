#include "SelectionSystem.hpp"
#include "../Components/Camera.hpp"
#include "../Components/Light.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <iostream>
#include <limits>
#include <algorithm>

namespace Crescent {

std::vector<Entity*> SelectionSystem::s_selectedEntities;

SelectionSystem::SelectionSystem() {
}

SelectionSystem::~SelectionSystem() {
}

Ray SelectionSystem::screenPointToRay(const Math::Vector2& screenPoint, 
                                      const Math::Vector2& screenSize,
                                      Camera* camera) {
    if (!camera) return Ray();
    
    // Screen -> NDC (Metal origin bottom-left)
    float ndcX = (screenPoint.x / screenSize.x) * 2.0f - 1.0f;
    float ndcY = (screenPoint.y / screenSize.y) * 2.0f - 1.0f;
    
    // Unproject using inverse view-projection
    Math::Matrix4x4 invViewProj = camera->getViewProjectionMatrix().inversed();
    
    Math::Vector4 farPoint(ndcX, ndcY, 1.0f, 1.0f);  // Far plane (z=1)
    Math::Vector4 farWorld = invViewProj * farPoint;
    
    if (farWorld.w != 0.0f) {
        farWorld.x /= farWorld.w;
        farWorld.y /= farWorld.w;
        farWorld.z /= farWorld.w;
    }
    
    Math::Vector3 camPos = camera->getEntity()->getTransform()->getPosition();
    Math::Vector3 rayDir = (Math::Vector3(farWorld.x, farWorld.y, farWorld.z) - camPos).normalized();
    
    return Ray(camPos, rayDir);
}

bool SelectionSystem::rayIntersectsAABB(const Ray& ray, const AABB& aabb, float& distance) {
    // Slab method - Andrew Woo's algorithm
    // https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-box-intersection
    
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();
    
    for (int i = 0; i < 3; ++i) {
        float rayOrigin = (&ray.origin.x)[i];
        float rayDir = (&ray.direction.x)[i];
        float boxMin = (&aabb.min.x)[i];
        float boxMax = (&aabb.max.x)[i];
        
        if (std::abs(rayDir) < 1e-8f) {
            // Ray is parallel to slab
            if (rayOrigin < boxMin || rayOrigin > boxMax) {
                return false;
            }
        } else {
            // Compute intersection t values
            float t1 = (boxMin - rayOrigin) / rayDir;
            float t2 = (boxMax - rayOrigin) / rayDir;
            
            if (t1 > t2) std::swap(t1, t2);
            
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            
            if (tmin > tmax) {
                return false;
            }
        }
    }
    
    distance = tmin;
    return true;
}

bool SelectionSystem::raycastEntity(const Ray& ray, Entity* entity, RaycastHit& hit) {
    if (!entity) return false;
    
    AABB bounds = getEntityBounds(entity);
    
    static int raycastDebugCount = 0;
    if (raycastDebugCount < 3) {
        std::cout << "[RAYCAST ENTITY] Testing: " << entity->getName() 
                  << " AABB min=(" << bounds.min.x << "," << bounds.min.y << "," << bounds.min.z << ")" 
                  << " max=(" << bounds.max.x << "," << bounds.max.y << "," << bounds.max.z << ")" << std::endl;
        raycastDebugCount++;
    }
    
    float distance;
    if (rayIntersectsAABB(ray, bounds, distance)) {
        hit.hit = true;
        hit.distance = distance;
        hit.point = ray.getPoint(distance);
        hit.entity = entity;
        
        if (raycastDebugCount < 3) {
            std::cout << "[RAYCAST HIT] " << entity->getName() << " at distance " << distance << std::endl;
        }
        return true;
    }
    
    return false;
}

RaycastHit SelectionSystem::raycastAll(const Ray& ray, const std::vector<Entity*>& entities) {
    RaycastHit closestHit;
    closestHit.distance = std::numeric_limits<float>::max();
    
    static int skipDebugCount = 0;
    
    for (Entity* entity : entities) {
        if (!entity || !entity->isActiveInHierarchy()) continue;
        if (entity->isEditorOnly()) {
            continue;
        }
        
        // CRITICAL: Skip scene entities - they should never be selectable!
        std::string entityName = entity->getName();
        if (entityName == "Main Camera" || entityName == "Directional Light" || entityName == "Editor Gizmo") {
            if (skipDebugCount < 3) {
                std::cout << "[RAYCAST] Skipping scene entity: " << entityName << std::endl;
                skipDebugCount++;
            }
            continue;
        }
        
        // Also skip by component check
        if (entity->hasComponent<Camera>()) {
            if (skipDebugCount < 3) {
                std::cout << "[RAYCAST] Skipping camera: " << entityName << std::endl;
            }
            continue;
        }
        
        // Optional: Skip light entities (no visual representation)
        if (entity->hasComponent<Light>() && !entity->hasComponent<MeshRenderer>()) {
            if (skipDebugCount < 3) {
                std::cout << "[RAYCAST] Skipping light: " << entityName << std::endl;
            }
            continue;
        }
        
        RaycastHit hit;
        if (raycastEntity(ray, entity, hit)) {
            if (hit.distance < closestHit.distance) {
                closestHit = hit;
            }
        }
    }
    
    return closestHit;
}

AABB SelectionSystem::getEntityBounds(Entity* entity) {
    if (!entity) return AABB();
    
    // Try to get bounds from MeshRenderer
    MeshRenderer* meshRenderer = entity->getComponent<MeshRenderer>();
    if (meshRenderer && meshRenderer->getMesh()) {
        // Get mesh bounds directly from mesh
        auto mesh = meshRenderer->getMesh();
        Math::Vector3 meshMin = mesh->getBoundsMin();
        Math::Vector3 meshMax = mesh->getBoundsMax();
        
        // Transform bounds to world space (handles rotation)
        Transform* transform = entity->getTransform();
        Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
        
        Math::Vector3 corners[8] = {
            {meshMin.x, meshMin.y, meshMin.z},
            {meshMax.x, meshMin.y, meshMin.z},
            {meshMin.x, meshMax.y, meshMin.z},
            {meshMin.x, meshMin.y, meshMax.z},
            {meshMax.x, meshMax.y, meshMin.z},
            {meshMin.x, meshMax.y, meshMax.z},
            {meshMax.x, meshMin.y, meshMax.z},
            {meshMax.x, meshMax.y, meshMax.z},
        };
        
        Math::Vector3 worldMin(std::numeric_limits<float>::max());
        Math::Vector3 worldMax(std::numeric_limits<float>::lowest());
        
        for (const auto& corner : corners) {
            Math::Vector3 worldPt = worldMatrix.transformPoint(corner);
            worldMin = Math::Vector3::Min(worldMin, worldPt);
            worldMax = Math::Vector3::Max(worldMax, worldPt);
        }
        
        return AABB(worldMin, worldMax);
    }
    
    // Fallback: use transform position as point
    Transform* transform = entity->getTransform();
    Math::Vector3 pos = transform->getPosition();
    Math::Vector3 extent(0.5f, 0.5f, 0.5f);  // Small box
    return AABB(pos - extent, pos + extent);
}

void SelectionSystem::setSelectedEntity(Entity* entity) {
    s_selectedEntities.clear();
    if (entity) {
        s_selectedEntities.push_back(entity);
        
        std::cout << "\n=== SELECTED ENTITY ===" << std::endl;
        std::cout << "  Name: " << entity->getName() << std::endl;
        std::cout << "  UUID: " << entity->getUUID().toString() << std::endl;
        std::cout << "  Position: (" << entity->getTransform()->getPosition().x << ", "
                  << entity->getTransform()->getPosition().y << ", "
                  << entity->getTransform()->getPosition().z << ")" << std::endl;
        std::cout << "========================\n" << std::endl;
    } else {
        std::cout << "\n=== SELECTION CLEARED ===\n" << std::endl;
    }
}

void SelectionSystem::setSelection(const std::vector<Entity*>& entities) {
    s_selectedEntities = entities;
    if (!s_selectedEntities.empty()) {
        Entity* first = s_selectedEntities.front();
        std::cout << "\n=== SELECTED ENTITY ===" << std::endl;
        std::cout << "  Name: " << first->getName() << " (+"
                  << (s_selectedEntities.size() - 1) << " more)" << std::endl;
        std::cout << "  UUID: " << first->getUUID().toString() << std::endl;
        std::cout << "========================\n" << std::endl;
    } else {
        std::cout << "\n=== SELECTION CLEARED ===\n" << std::endl;
    }
}

void SelectionSystem::toggleSelection(Entity* entity) {
    if (!entity) return;
    auto it = std::find(s_selectedEntities.begin(), s_selectedEntities.end(), entity);
    if (it != s_selectedEntities.end()) {
        s_selectedEntities.erase(it);
    } else {
        s_selectedEntities.insert(s_selectedEntities.begin(), entity);
    }
}

void SelectionSystem::removeEntity(Entity* entity) {
    if (!entity) {
        return;
    }
    auto it = std::remove(s_selectedEntities.begin(), s_selectedEntities.end(), entity);
    if (it != s_selectedEntities.end()) {
        s_selectedEntities.erase(it, s_selectedEntities.end());
        if (s_selectedEntities.empty()) {
            std::cout << "\n=== SELECTION CLEARED ===\n" << std::endl;
        }
    }
}

Entity* SelectionSystem::getSelectedEntity() {
    if (!s_selectedEntities.empty()) return s_selectedEntities.front();
    return nullptr;
}

const std::vector<Entity*>& SelectionSystem::getSelection() {
    return s_selectedEntities;
}

void SelectionSystem::clearSelection() {
    s_selectedEntities.clear();
    std::cout << "\n=== SELECTION CLEARED ===\n" << std::endl;
}

} // namespace Crescent
