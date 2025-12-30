#pragma once

#include "../Math/Math.hpp"
#include <vector>

namespace Crescent {

// Forward declarations
class Entity;
class Camera;

// Ray for raycasting
struct Ray {
    Math::Vector3 origin;
    Math::Vector3 direction;
    
    Ray() = default;
    Ray(const Math::Vector3& origin, const Math::Vector3& direction)
        : origin(origin), direction(direction) {}
    
    Math::Vector3 getPoint(float distance) const {
        return origin + direction * distance;
    }
};

// Axis-aligned bounding box
struct AABB {
    Math::Vector3 min;
    Math::Vector3 max;
    
    AABB() : min(Math::Vector3::Zero), max(Math::Vector3::Zero) {}
    AABB(const Math::Vector3& min, const Math::Vector3& max) : min(min), max(max) {}
    
    Math::Vector3 center() const { return (min + max) * 0.5f; }
    Math::Vector3 size() const { return max - min; }
    Math::Vector3 extents() const { return size() * 0.5f; }
    
    bool contains(const Math::Vector3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    // Expand to include a point
    void encapsulate(const Math::Vector3& point) {
        min = Math::Vector3::Min(min, point);
        max = Math::Vector3::Max(max, point);
    }
    
    // Expand to include another AABB
    void encapsulate(const AABB& bounds) {
        encapsulate(bounds.min);
        encapsulate(bounds.max);
    }
};

// Ray-AABB intersection result
struct RaycastHit {
    bool hit;
    float distance;
    Math::Vector3 point;
    Entity* entity;
    
    RaycastHit() : hit(false), distance(0.0f), entity(nullptr) {}
};

// Selection and raycasting system
class SelectionSystem {
public:
    SelectionSystem();
    ~SelectionSystem();
    
    // Convert screen coordinates to world ray
    static Ray screenPointToRay(const Math::Vector2& screenPoint, 
                               const Math::Vector2& screenSize,
                               Camera* camera);
    
    // Ray-AABB intersection test
    static bool rayIntersectsAABB(const Ray& ray, const AABB& aabb, float& distance);
    
    // Raycast against entity (using its bounding box)
    static bool raycastEntity(const Ray& ray, Entity* entity, RaycastHit& hit);
    
    // Raycast against multiple entities, return closest
    static RaycastHit raycastAll(const Ray& ray, const std::vector<Entity*>& entities);
    
    // Get world-space AABB for entity
    static AABB getEntityBounds(Entity* entity);
    
    // Selected entity management
    static void setSelectedEntity(Entity* entity);
    static void setSelection(const std::vector<Entity*>& entities);
    static void toggleSelection(Entity* entity);
    static Entity* getSelectedEntity();
    static const std::vector<Entity*>& getSelection();
    static void clearSelection();
    
private:
    static std::vector<Entity*> s_selectedEntities;
};

} // namespace Crescent
