#include "MeshRenderer.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <limits>

namespace Crescent {

namespace {

void ComputeWorldAABB(const Mesh* mesh,
                      const Math::Matrix4x4& worldMatrix,
                      Math::Vector3& outMin,
                      Math::Vector3& outMax) {
    if (!mesh) {
        outMin = Math::Vector3::Zero;
        outMax = Math::Vector3::Zero;
        return;
    }

    const Math::Vector3 localMin = mesh->getBoundsMin();
    const Math::Vector3 localMax = mesh->getBoundsMax();
    const Math::Vector3 corners[8] = {
        Math::Vector3(localMin.x, localMin.y, localMin.z),
        Math::Vector3(localMax.x, localMin.y, localMin.z),
        Math::Vector3(localMin.x, localMax.y, localMin.z),
        Math::Vector3(localMax.x, localMax.y, localMin.z),
        Math::Vector3(localMin.x, localMin.y, localMax.z),
        Math::Vector3(localMax.x, localMin.y, localMax.z),
        Math::Vector3(localMin.x, localMax.y, localMax.z),
        Math::Vector3(localMax.x, localMax.y, localMax.z)
    };

    outMin = Math::Vector3(std::numeric_limits<float>::max());
    outMax = Math::Vector3(std::numeric_limits<float>::lowest());
    for (const Math::Vector3& corner : corners) {
        Math::Vector3 worldCorner = worldMatrix.transformPoint(corner);
        outMin = Math::Vector3::Min(outMin, worldCorner);
        outMax = Math::Vector3::Max(outMax, worldCorner);
    }
}

} // namespace

MeshRenderer::MeshRenderer()
    : m_Mesh(nullptr)
    , m_CastShadows(true)
    , m_ReceiveShadows(true) {
    
    // Default material
    m_Materials.push_back(Material::CreateDefault());
}

void MeshRenderer::setMesh(std::shared_ptr<Mesh> mesh) {
    m_Mesh = mesh;
}

void MeshRenderer::setMaterial(std::shared_ptr<Material> material) {
    if (m_Materials.empty()) {
        m_Materials.push_back(material);
    } else {
        m_Materials[0] = material;
    }
}

void MeshRenderer::setMaterial(uint32_t index, std::shared_ptr<Material> material) {
    if (index >= m_Materials.size()) {
        m_Materials.resize(index + 1);
    }
    m_Materials[index] = material;
}

std::shared_ptr<Material> MeshRenderer::getMaterial(uint32_t index) const {
    if (index < m_Materials.size()) {
        return m_Materials[index];
    }
    return nullptr;
}

Math::Vector3 MeshRenderer::getBoundsMin() const {
    if (!m_Mesh) return Math::Vector3::Zero;
    
    Transform* transform = m_Entity->getTransform();
    Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
    Math::Vector3 worldMin;
    Math::Vector3 worldMax;
    ComputeWorldAABB(m_Mesh.get(), worldMatrix, worldMin, worldMax);
    return worldMin;
}

Math::Vector3 MeshRenderer::getBoundsMax() const {
    if (!m_Mesh) return Math::Vector3::Zero;
    
    Transform* transform = m_Entity->getTransform();
    Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
    Math::Vector3 worldMin;
    Math::Vector3 worldMax;
    ComputeWorldAABB(m_Mesh.get(), worldMatrix, worldMin, worldMax);
    return worldMax;
}

Math::Vector3 MeshRenderer::getBoundsCenter() const {
    if (!m_Mesh) return Math::Vector3::Zero;
    
    Transform* transform = m_Entity->getTransform();
    Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
    Math::Vector3 worldMin;
    Math::Vector3 worldMax;
    ComputeWorldAABB(m_Mesh.get(), worldMatrix, worldMin, worldMax);
    return (worldMin + worldMax) * 0.5f;
}

Math::Vector3 MeshRenderer::getBoundsSize() const {
    if (!m_Mesh) return Math::Vector3::Zero;

    Transform* transform = m_Entity->getTransform();
    Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
    Math::Vector3 worldMin;
    Math::Vector3 worldMax;
    ComputeWorldAABB(m_Mesh.get(), worldMatrix, worldMin, worldMax);
    return worldMax - worldMin;
}

void MeshRenderer::OnCreate() {
    // Register with rendering system
}

void MeshRenderer::OnDestroy() {
    // Unregister from rendering system
}

} // namespace Crescent
