#include "MeshRenderer.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <cmath>
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
    , m_ReceiveShadows(true)
    , m_UseBakedVertexLighting(false) {
    
    // Default material
    m_Materials.push_back(Material::CreateDefault());
}

void MeshRenderer::setMesh(std::shared_ptr<Mesh> mesh) {
    m_Mesh = mesh;
    if (m_Mesh && !m_BakedVertexColors.empty()) {
        setBakedVertexColors(m_BakedVertexColors);
    }
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

void MeshRenderer::setBakedVertexColors(const std::vector<Math::Vector4>& colors) {
    m_BakedVertexColors = colors;
    m_UseBakedVertexLighting = !m_BakedVertexColors.empty();
    if (!m_Mesh) {
        return;
    }

    const auto& sourceVertices = m_Mesh->getVertices();
    if (sourceVertices.empty() || sourceVertices.size() != m_BakedVertexColors.size()) {
        m_UseBakedVertexLighting = false;
        return;
    }

    std::vector<Vertex> bakedVertices = sourceVertices;
    for (size_t i = 0; i < bakedVertices.size(); ++i) {
        bakedVertices[i].color = m_BakedVertexColors[i];
    }
    m_Mesh->setVertices(bakedVertices);
}

void MeshRenderer::clearBakedVertexLighting() {
    m_UseBakedVertexLighting = false;
    m_BakedVertexColors.clear();
}

bool MeshRenderer::hasStaticLightingData() const {
    const Math::Vector4& scaleOffset = m_StaticLighting.lightmapScaleOffset;
    bool hasCustomScaleOffset = std::abs(scaleOffset.x - 1.0f) > 0.0001f ||
                                std::abs(scaleOffset.y - 1.0f) > 0.0001f ||
                                std::abs(scaleOffset.z) > 0.0001f ||
                                std::abs(scaleOffset.w) > 0.0001f;
    return m_StaticLighting.staticGeometry ||
           !m_StaticLighting.contributeGI ||
           !m_StaticLighting.receiveGI ||
           m_StaticLighting.lightmapIndex >= 0 ||
           m_StaticLighting.lightmapUVChannel != 1 ||
           hasCustomScaleOffset ||
           !m_StaticLighting.lightmapPath.empty() ||
           !m_StaticLighting.directionalLightmapPath.empty() ||
           !m_StaticLighting.shadowmaskPath.empty();
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
