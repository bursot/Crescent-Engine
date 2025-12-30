#include "Material.hpp"

namespace Crescent {

Material::Material(const std::string& name)
    : m_Name(name)
    , m_Albedo(1.0f, 1.0f, 1.0f, 1.0f)
    , m_Metallic(0.0f)
    , m_Roughness(0.5f)
    , m_AO(1.0f)
    , m_Emission(0.0f, 0.0f, 0.0f)
    , m_EmissionStrength(0.0f)
    , m_RenderMode(RenderMode::Opaque)
    , m_AlphaCutoff(0.5f)
    , m_CullMode(CullMode::Back)
    , m_TwoSided(false)
    , m_AlbedoTexture(nullptr)
    , m_NormalTexture(nullptr)
    , m_MetallicTexture(nullptr)
    , m_RoughnessTexture(nullptr)
    , m_AOTexture(nullptr)
    , m_EmissionTexture(nullptr)
    , m_ORMTexture(nullptr)
    , m_UVTiling(1.0f, 1.0f)
    , m_UVOffset(0.0f, 0.0f)
    , m_NormalScale(1.0f)
    , m_HeightScale(0.02f)
    , m_HeightInvert(false)
    , m_Shader(nullptr) {
}

Material::~Material() {
    // Textures and shader are managed by resource system
}

std::shared_ptr<Material> Material::CreateDefault() {
    auto material = std::make_shared<Material>("Default");
    material->setAlbedo(Math::Vector4(0.8f, 0.8f, 0.8f, 1.0f));
    material->setMetallic(0.0f);
    material->setRoughness(0.5f);
    return material;
}

std::shared_ptr<Material> Material::CreateUnlit(const Math::Vector4& color) {
    auto material = std::make_shared<Material>("Unlit");
    material->setAlbedo(color);
    material->setMetallic(0.0f);
    material->setRoughness(1.0f);
    return material;
}

std::shared_ptr<Material> Material::CreateMetal(const Math::Vector3& albedo, float roughness) {
    auto material = std::make_shared<Material>("Metal");
    material->setAlbedo(albedo);
    material->setMetallic(1.0f);
    material->setRoughness(roughness);
    return material;
}

std::shared_ptr<Material> Material::CreatePlastic(const Math::Vector3& albedo, float roughness) {
    auto material = std::make_shared<Material>("Plastic");
    material->setAlbedo(albedo);
    material->setMetallic(0.0f);
    material->setRoughness(roughness);
    return material;
}

std::shared_ptr<Material> Material::CreateGlass(const Math::Vector3& tint, float roughness) {
    auto material = std::make_shared<Material>("Glass");
    material->setAlbedo(Math::Vector4(tint.x, tint.y, tint.z, 0.1f));
    material->setMetallic(0.0f);
    material->setRoughness(roughness);
    material->setRenderMode(RenderMode::Transparent);
    material->setTwoSided(true);
    return material;
}

std::shared_ptr<Material> Material::CreateEmissive(const Math::Vector3& color, float strength) {
    auto material = std::make_shared<Material>("Emissive");
    material->setAlbedo(Math::Vector3(0.1f, 0.1f, 0.1f));
    material->setEmission(color);
    material->setEmissionStrength(strength);
    material->setMetallic(0.0f);
    material->setRoughness(1.0f);
    return material;
}

} // namespace Crescent
