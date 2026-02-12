#pragma once

#include "../Math/Math.hpp"
#include "Texture.hpp"
#include <string>
#include <memory>
#include <unordered_map>

namespace Crescent {

// Material properties for PBR rendering
class Material {
public:
    Material(const std::string& name = "Material");
    ~Material();
    
    // Material properties
    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }
    
    // Albedo/Base Color
    const Math::Vector4& getAlbedo() const { return m_Albedo; }
    void setAlbedo(const Math::Vector4& albedo) { m_Albedo = albedo; }
    void setAlbedo(const Math::Vector3& rgb) { m_Albedo = Math::Vector4(rgb.x, rgb.y, rgb.z, 1.0f); }
    
    // Metallic workflow
    float getMetallic() const { return m_Metallic; }
    void setMetallic(float metallic) { m_Metallic = Math::Clamp(metallic, 0.0f, 1.0f); }
    
    float getRoughness() const { return m_Roughness; }
    void setRoughness(float roughness) { m_Roughness = Math::Clamp(roughness, 0.0f, 1.0f); }
    
    // Ambient Occlusion
    float getAO() const { return m_AO; }
    void setAO(float ao) { m_AO = Math::Clamp(ao, 0.0f, 1.0f); }
    
    // Emission
    const Math::Vector3& getEmission() const { return m_Emission; }
    void setEmission(const Math::Vector3& emission) { m_Emission = emission; }
    
    float getEmissionStrength() const { return m_EmissionStrength; }
    void setEmissionStrength(float strength) { m_EmissionStrength = std::max(0.0f, strength); }
    
    // Transparency
    float getAlpha() const { return m_Albedo.w; }
    void setAlpha(float alpha) { m_Albedo.w = Math::Clamp(alpha, 0.0f, 1.0f); }
    
    bool isTransparent() const { return m_Albedo.w < 1.0f; }
    
    // UV transform
    const Math::Vector2& getUVTiling() const { return m_UVTiling; }
    void setUVTiling(const Math::Vector2& tiling) { m_UVTiling = tiling; }
    
    const Math::Vector2& getUVOffset() const { return m_UVOffset; }
    void setUVOffset(const Math::Vector2& offset) { m_UVOffset = offset; }
    
    float getNormalScale() const { return m_NormalScale; }
    void setNormalScale(float scale) { m_NormalScale = scale; }
    
    float getHeightScale() const { return m_HeightScale; }
    void setHeightScale(float scale) { m_HeightScale = Math::Clamp(scale, 0.0f, 0.2f); }
    
    bool getHeightInvert() const { return m_HeightInvert; }
    void setHeightInvert(bool invert) { m_HeightInvert = invert; }
    
    // Rendering properties
    enum class RenderMode {
        Opaque,
        Transparent,
        Cutout
    };
    
    RenderMode getRenderMode() const { return m_RenderMode; }
    void setRenderMode(RenderMode mode) { m_RenderMode = mode; }
    
    float getAlphaCutoff() const { return m_AlphaCutoff; }
    void setAlphaCutoff(float cutoff) { m_AlphaCutoff = Math::Clamp(cutoff, 0.0f, 1.0f); }
    
    // Culling
    enum class CullMode {
        Off,
        Front,
        Back
    };
    
    CullMode getCullMode() const { return m_CullMode; }
    void setCullMode(CullMode mode) { m_CullMode = mode; }
    
    // Two-sided rendering
    bool isTwoSided() const { return m_TwoSided; }
    void setTwoSided(bool twoSided) { m_TwoSided = twoSided; }

    // Alpha-to-coverage (MSAA only)
    bool getAlphaToCoverage() const { return m_AlphaToCoverage; }
    void setAlphaToCoverage(bool enabled) { m_AlphaToCoverage = enabled; }

    // Foliage/wind/LOD controls
    bool getWindEnabled() const { return m_WindEnabled; }
    void setWindEnabled(bool enabled) { m_WindEnabled = enabled; }

    float getWindStrength() const { return m_WindStrength; }
    void setWindStrength(float strength) { m_WindStrength = std::max(0.0f, strength); }

    float getWindSpeed() const { return m_WindSpeed; }
    void setWindSpeed(float speed) { m_WindSpeed = speed; }

    float getWindScale() const { return m_WindScale; }
    void setWindScale(float scale) { m_WindScale = std::max(0.0f, scale); }

    float getWindGust() const { return m_WindGust; }
    void setWindGust(float gust) { m_WindGust = std::max(0.0f, gust); }

    const Math::Vector3& getWindDirection() const { return m_WindDirection; }
    void setWindDirection(const Math::Vector3& dir) { m_WindDirection = dir; }

    bool getLodFadeEnabled() const { return m_LodFadeEnabled; }
    void setLodFadeEnabled(bool enabled) { m_LodFadeEnabled = enabled; }

    float getLodFadeStart() const { return m_LodFadeStart; }
    void setLodFadeStart(float start) { m_LodFadeStart = std::max(0.0f, start); }

    float getLodFadeEnd() const { return m_LodFadeEnd; }
    void setLodFadeEnd(float end) { m_LodFadeEnd = std::max(0.0f, end); }

    bool getDitherEnabled() const { return m_DitherEnabled; }
    void setDitherEnabled(bool enabled) { m_DitherEnabled = enabled; }

    bool getBillboardEnabled() const { return m_BillboardEnabled; }
    void setBillboardEnabled(bool enabled) { m_BillboardEnabled = enabled; }

    float getBillboardStart() const { return m_BillboardStart; }
    void setBillboardStart(float start) { m_BillboardStart = std::max(0.0f, start); }

    float getBillboardEnd() const { return m_BillboardEnd; }
    void setBillboardEnd(float end) { m_BillboardEnd = std::max(0.0f, end); }

    // Impostor atlas (billboard LOD)
    bool getImpostorEnabled() const { return m_ImpostorEnabled; }
    void setImpostorEnabled(bool enabled) { m_ImpostorEnabled = enabled; }

    int getImpostorRows() const { return m_ImpostorRows; }
    void setImpostorRows(int rows) { m_ImpostorRows = std::max(1, rows); }

    int getImpostorCols() const { return m_ImpostorCols; }
    void setImpostorCols(int cols) { m_ImpostorCols = std::max(1, cols); }
    
    // Textures (stored as void* to avoid Metal types in header)
    std::shared_ptr<Texture2D> getAlbedoTexture() const { return m_AlbedoTexture; }
    void setAlbedoTexture(std::shared_ptr<Texture2D> texture) { m_AlbedoTexture = texture; }
    
    std::shared_ptr<Texture2D> getNormalTexture() const { return m_NormalTexture; }
    void setNormalTexture(std::shared_ptr<Texture2D> texture) { m_NormalTexture = texture; }
    
    std::shared_ptr<Texture2D> getMetallicTexture() const { return m_MetallicTexture; }
    void setMetallicTexture(std::shared_ptr<Texture2D> texture) { m_MetallicTexture = texture; }
    
    std::shared_ptr<Texture2D> getRoughnessTexture() const { return m_RoughnessTexture; }
    void setRoughnessTexture(std::shared_ptr<Texture2D> texture) { m_RoughnessTexture = texture; }
    
    std::shared_ptr<Texture2D> getAOTexture() const { return m_AOTexture; }
    void setAOTexture(std::shared_ptr<Texture2D> texture) { m_AOTexture = texture; }
    
    std::shared_ptr<Texture2D> getEmissionTexture() const { return m_EmissionTexture; }
    void setEmissionTexture(std::shared_ptr<Texture2D> texture) { m_EmissionTexture = texture; }
    
    std::shared_ptr<Texture2D> getORMTexture() const { return m_ORMTexture; }
    void setORMTexture(std::shared_ptr<Texture2D> texture) { m_ORMTexture = texture; }
    
    std::shared_ptr<Texture2D> getHeightTexture() const { return m_HeightTexture; }
    void setHeightTexture(std::shared_ptr<Texture2D> texture) { m_HeightTexture = texture; }

    std::shared_ptr<Texture2D> getOpacityTexture() const { return m_OpacityTexture; }
    void setOpacityTexture(std::shared_ptr<Texture2D> texture) { m_OpacityTexture = texture; }
    
    // Shader
    void* getShader() const { return m_Shader; }
    void setShader(void* shader) { m_Shader = shader; }
    
    // Material presets
    static std::shared_ptr<Material> CreateDefault();
    static std::shared_ptr<Material> CreateUnlit(const Math::Vector4& color);
    static std::shared_ptr<Material> CreateMetal(const Math::Vector3& albedo, float roughness = 0.2f);
    static std::shared_ptr<Material> CreatePlastic(const Math::Vector3& albedo, float roughness = 0.5f);
    static std::shared_ptr<Material> CreateGlass(const Math::Vector3& tint = Math::Vector3(1, 1, 1), float roughness = 0.0f);
    static std::shared_ptr<Material> CreateEmissive(const Math::Vector3& color, float strength = 1.0f);
    
private:
    std::string m_Name;
    
    // PBR properties
    Math::Vector4 m_Albedo;          // RGB + Alpha
    float m_Metallic;
    float m_Roughness;
    float m_AO;
    
    Math::Vector3 m_Emission;
    float m_EmissionStrength;
    
    // Rendering
    RenderMode m_RenderMode;
    float m_AlphaCutoff;
    CullMode m_CullMode;
    bool m_TwoSided;
    bool m_AlphaToCoverage;

    bool m_WindEnabled;
    float m_WindStrength;
    float m_WindSpeed;
    float m_WindScale;
    float m_WindGust;
    Math::Vector3 m_WindDirection;

    bool m_LodFadeEnabled;
    float m_LodFadeStart;
    float m_LodFadeEnd;
    bool m_DitherEnabled;

    bool m_BillboardEnabled;
    float m_BillboardStart;
    float m_BillboardEnd;
    bool m_ImpostorEnabled;
    int m_ImpostorRows;
    int m_ImpostorCols;
    
    // Textures (as void* to avoid Metal types)
    std::shared_ptr<Texture2D> m_AlbedoTexture;
    std::shared_ptr<Texture2D> m_NormalTexture;
    std::shared_ptr<Texture2D> m_MetallicTexture;
    std::shared_ptr<Texture2D> m_RoughnessTexture;
    std::shared_ptr<Texture2D> m_AOTexture;
    std::shared_ptr<Texture2D> m_EmissionTexture;
    std::shared_ptr<Texture2D> m_ORMTexture;
    std::shared_ptr<Texture2D> m_HeightTexture;
    std::shared_ptr<Texture2D> m_OpacityTexture;
    
    Math::Vector2 m_UVTiling;
    Math::Vector2 m_UVOffset;
    float m_NormalScale;
    float m_HeightScale;
    bool m_HeightInvert;
    
    // Shader
    void* m_Shader;
};

} // namespace Crescent
