#include "MeshRenderer.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"

namespace Crescent {

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
    
    Math::Vector3 meshMin = m_Mesh->getBoundsMin();
    return worldMatrix.transformPoint(meshMin);
}

Math::Vector3 MeshRenderer::getBoundsMax() const {
    if (!m_Mesh) return Math::Vector3::Zero;
    
    Transform* transform = m_Entity->getTransform();
    Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
    
    Math::Vector3 meshMax = m_Mesh->getBoundsMax();
    return worldMatrix.transformPoint(meshMax);
}

Math::Vector3 MeshRenderer::getBoundsCenter() const {
    if (!m_Mesh) return Math::Vector3::Zero;
    
    Transform* transform = m_Entity->getTransform();
    Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
    
    Math::Vector3 meshCenter = m_Mesh->getBoundsCenter();
    return worldMatrix.transformPoint(meshCenter);
}

Math::Vector3 MeshRenderer::getBoundsSize() const {
    if (!m_Mesh) return Math::Vector3::Zero;
    
    Math::Vector3 scale = m_Entity->getTransform()->getScale();
    Math::Vector3 meshSize = m_Mesh->getBoundsSize();
    
    return Math::Vector3(
        meshSize.x * std::abs(scale.x),
        meshSize.y * std::abs(scale.y),
        meshSize.z * std::abs(scale.z)
    );
}

void MeshRenderer::OnCreate() {
    // Register with rendering system
}

void MeshRenderer::OnDestroy() {
    // Unregister from rendering system
}

} // namespace Crescent
