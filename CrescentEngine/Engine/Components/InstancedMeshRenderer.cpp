#include "InstancedMeshRenderer.hpp"

namespace Crescent {

InstancedMeshRenderer::InstancedMeshRenderer()
    : m_Mesh(nullptr)
    , m_CastShadows(true)
    , m_ReceiveShadows(true) {
    m_Materials.push_back(Material::CreateDefault());
}

void InstancedMeshRenderer::setMesh(std::shared_ptr<Mesh> mesh) {
    m_Mesh = mesh;
}

void InstancedMeshRenderer::setMaterial(std::shared_ptr<Material> material) {
    if (m_Materials.empty()) {
        m_Materials.push_back(material);
    } else {
        m_Materials[0] = material;
    }
}

void InstancedMeshRenderer::setMaterial(uint32_t index, std::shared_ptr<Material> material) {
    if (index >= m_Materials.size()) {
        m_Materials.resize(index + 1);
    }
    m_Materials[index] = material;
}

std::shared_ptr<Material> InstancedMeshRenderer::getMaterial(uint32_t index) const {
    if (index < m_Materials.size()) {
        return m_Materials[index];
    }
    return nullptr;
}

void InstancedMeshRenderer::clearInstances() {
    m_Instances.clear();
}

void InstancedMeshRenderer::addInstance(const Math::Matrix4x4& localTransform) {
    m_Instances.push_back(localTransform);
}

void InstancedMeshRenderer::setInstances(const std::vector<Math::Matrix4x4>& localTransforms) {
    m_Instances = localTransforms;
}

void InstancedMeshRenderer::OnCreate() {}

void InstancedMeshRenderer::OnDestroy() {}

} // namespace Crescent
