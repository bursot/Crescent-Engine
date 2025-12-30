#pragma once

#include "../ECS/Component.hpp"

namespace Crescent {

enum class PrimitiveType {
    Cube,
    Sphere,
    Plane,
    Cylinder,
    Cone,
    Torus,
    Capsule
};

class PrimitiveMesh : public Component {
public:
    PrimitiveMesh() = default;
    explicit PrimitiveMesh(PrimitiveType type)
        : m_Type(type) {}

    COMPONENT_TYPE(PrimitiveMesh)

    PrimitiveType getType() const { return m_Type; }
    void setType(PrimitiveType type) { m_Type = type; }

private:
    PrimitiveType m_Type = PrimitiveType::Cube;
};

} // namespace Crescent
