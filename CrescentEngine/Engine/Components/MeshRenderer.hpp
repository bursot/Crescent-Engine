#pragma once

#include "../ECS/Component.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Material.hpp"
#include <vector>
#include <memory>

namespace Crescent {

// MeshRenderer component - renders a mesh with materials
class MeshRenderer : public Component {
public:
    MeshRenderer();
    virtual ~MeshRenderer() = default;
    
    COMPONENT_TYPE(MeshRenderer)
    
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
    
    // Bounds for culling
    Math::Vector3 getBoundsMin() const;
    Math::Vector3 getBoundsMax() const;
    Math::Vector3 getBoundsCenter() const;
    Math::Vector3 getBoundsSize() const;
    
    // Lifecycle
    void OnCreate() override;
    void OnDestroy() override;
    
private:
    std::shared_ptr<Mesh> m_Mesh;
    std::vector<std::shared_ptr<Material>> m_Materials;
    
    bool m_CastShadows;
    bool m_ReceiveShadows;
};

} // namespace Crescent
