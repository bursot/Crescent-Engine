#pragma once

#include "../ECS/Component.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Material.hpp"
#include <vector>
#include <memory>

namespace Crescent {

// InstancedMeshRenderer - renders many instances of the same mesh/material set.
// Instance transforms are local to the entity (entity transform is applied as parent).
class InstancedMeshRenderer : public Component {
public:
    InstancedMeshRenderer();
    virtual ~InstancedMeshRenderer() = default;

    COMPONENT_TYPE(InstancedMeshRenderer)

    // Mesh
    std::shared_ptr<Mesh> getMesh() const { return m_Mesh; }
    void setMesh(std::shared_ptr<Mesh> mesh);

    // Materials
    void setMaterial(std::shared_ptr<Material> material);
    void setMaterial(uint32_t index, std::shared_ptr<Material> material);
    std::shared_ptr<Material> getMaterial(uint32_t index = 0) const;
    const std::vector<std::shared_ptr<Material>>& getMaterials() const { return m_Materials; }

    uint32_t getMaterialCount() const { return static_cast<uint32_t>(m_Materials.size()); }

    // Rendering properties
    bool getCastShadows() const { return m_CastShadows; }
    void setCastShadows(bool cast) { m_CastShadows = cast; }

    bool getReceiveShadows() const { return m_ReceiveShadows; }
    void setReceiveShadows(bool receive) { m_ReceiveShadows = receive; }

    // Instances (local to entity)
    void clearInstances();
    void addInstance(const Math::Matrix4x4& localTransform);
    void setInstances(const std::vector<Math::Matrix4x4>& localTransforms);
    const std::vector<Math::Matrix4x4>& getInstances() const { return m_Instances; }
    uint32_t getInstanceCount() const { return static_cast<uint32_t>(m_Instances.size()); }

    // Lifecycle
    void OnCreate() override;
    void OnDestroy() override;

private:
    std::shared_ptr<Mesh> m_Mesh;
    std::vector<std::shared_ptr<Material>> m_Materials;
    std::vector<Math::Matrix4x4> m_Instances;

    bool m_CastShadows;
    bool m_ReceiveShadows;
};

} // namespace Crescent
