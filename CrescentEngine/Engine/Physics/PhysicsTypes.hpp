#pragma once

#include "../Math/Math.hpp"

namespace Crescent {

class Entity;

struct PhysicsRaycastHit {
    bool hit = false;
    float distance = 0.0f;
    Math::Vector3 point = Math::Vector3::Zero;
    Math::Vector3 normal = Math::Vector3::Up;
    Entity* entity = nullptr;
    bool isTrigger = false;
};

struct PhysicsOverlapHit {
    Math::Vector3 point = Math::Vector3::Zero;
    Math::Vector3 normal = Math::Vector3::Up;
    float penetration = 0.0f;
    Entity* entity = nullptr;
    bool isTrigger = false;
};

struct PhysicsContact {
    Math::Vector3 point = Math::Vector3::Zero;
    Math::Vector3 normal = Math::Vector3::Up;
    float penetration = 0.0f;
    Entity* other = nullptr;
    bool isTrigger = false;
};

} // namespace Crescent
