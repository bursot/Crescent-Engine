#pragma once

#include "../ECS/Component.hpp"
#include "../Math/Math.hpp"
#include <array>
#include <cstdint>
#include <algorithm>

namespace Crescent {

// Light component - various light types
class Light : public Component {
public:
    enum class Type {
        Directional,  // Sun-like, parallel rays
        Point,        // Omnidirectional point light
        Spot,         // Cone-shaped spotlight
        AreaRect,     // Rectangular area light
        AreaDisk,     // Disc area light
        EmissiveMesh  // Uses mesh emission
    };
    
    enum class IntensityUnit {
        Unitless,
        Lumens,   // Point/spot luminous flux
        Lux,      // Illuminance at 1m
        Candelas, // Directional luminous intensity
        Nits      // Area/dir cd/m^2
    };
    
    enum class FalloffModel {
        Linear,
        InverseSquare
    };
    
    enum class ShadowQuality {
        Low,
        Medium,
        High,
        Ultra
    };
    
    Light();
    virtual ~Light() = default;
    
    COMPONENT_TYPE(Light)
    
    // Light type
    Type getType() const { return m_Type; }
    void setType(Type type) { m_Type = type; }
    
    // Color and intensity
    const Math::Vector3& getColor() const { return m_Color; }
    void setColor(const Math::Vector3& color) { m_Color = color; }
    
    float getColorTemperature() const { return m_ColorTemperature; }
    void setColorTemperature(float kelvin);
    
    IntensityUnit getIntensityUnit() const { return m_IntensityUnit; }
    void setIntensityUnit(IntensityUnit unit) { m_IntensityUnit = unit; }
    
    float getIntensity() const { return m_Intensity; }
    void setIntensity(float intensity) { m_Intensity = std::max(0.0f, intensity); }
    
    // Range (for point and spot lights)
    float getRange() const { return m_Range; }
    void setRange(float range) { m_Range = std::max(0.0f, range); }
    
    // Spot light properties
    float getSpotAngle() const { return m_SpotAngle; }
    void setSpotAngle(float angle) { m_SpotAngle = Math::Clamp(angle, 0.0f, 180.0f); }
    
    float getInnerSpotAngle() const { return m_InnerSpotAngle; }
    void setInnerSpotAngle(float angle) { m_InnerSpotAngle = Math::Clamp(angle, 0.0f, m_SpotAngle); }
    
    // Area light properties
    Math::Vector2 getAreaSize() const { return m_AreaSize; } // width/height for rect, radius in x for disk
    void setAreaSize(const Math::Vector2& size);
    float getSourceRadius() const { return m_SourceRadius; }
    void setSourceRadius(float radius) { m_SourceRadius = std::max(0.0f, radius); }
    float getSourceLength() const { return m_SourceLength; }
    void setSourceLength(float length) { m_SourceLength = std::max(0.0f, length); }
    
    // Shadow casting
    bool getCastShadows() const { return m_CastShadows; }
    void setCastShadows(bool cast) { m_CastShadows = cast; }
    bool getSoftShadows() const { return m_SoftShadows; }
    void setSoftShadows(bool soft) { m_SoftShadows = soft; }
    bool getContactShadows() const { return m_ContactShadows; }
    void setContactShadows(bool contact) { m_ContactShadows = contact; }
    
    // Shadow properties
    float getShadowBias() const { return m_ShadowBias; }
    void setShadowBias(float bias) { m_ShadowBias = bias; }
    
    float getShadowNormalBias() const { return m_ShadowNormalBias; }
    void setShadowNormalBias(float bias) { m_ShadowNormalBias = bias; }
    
    ShadowQuality getShadowQuality() const { return m_ShadowQuality; }
    void setShadowQuality(ShadowQuality quality) { m_ShadowQuality = quality; }
    
    uint32_t getShadowMapResolution() const { return m_ShadowMapResolution; }
    void setShadowMapResolution(uint32_t resolution) { m_ShadowMapResolution = resolution; }
    
    uint8_t getCascadeCount() const { return m_CascadeCount; }
    void setCascadeCount(uint8_t cascades);
    const std::array<float, 4>& getCascadeSplits() const { return m_CascadeSplits; }
    void setCascadeSplits(const std::array<float, 4>& splits);
    float getShadowNearPlane() const { return m_ShadowNearPlane; }
    float getShadowFarPlane() const { return m_ShadowFarPlane; }
    void setShadowRange(float nearPlane, float farPlane);
    float getPenumbra() const { return m_Penumbra; }
    void setPenumbra(float penumbra) { m_Penumbra = std::max(0.0f, penumbra); }
    
    // Attenuation (for point and spot lights)
    float getConstantAttenuation() const { return m_ConstantAttenuation; }
    void setConstantAttenuation(float value) { m_ConstantAttenuation = std::max(0.0f, value); }
    
    float getLinearAttenuation() const { return m_LinearAttenuation; }
    void setLinearAttenuation(float value) { m_LinearAttenuation = std::max(0.0f, value); }
    
    float getQuadraticAttenuation() const { return m_QuadraticAttenuation; }
    void setQuadraticAttenuation(float value) { m_QuadraticAttenuation = std::max(0.0f, value); }
    
    FalloffModel getFalloffModel() const { return m_FalloffModel; }
    void setFalloffModel(FalloffModel model) { m_FalloffModel = model; }
    
    // Cookies / IES
    int32_t getCookieIndex() const { return m_CookieIndex; }
    void setCookieIndex(int32_t idx) { m_CookieIndex = idx; }
    int32_t getIESProfileIndex() const { return m_IESProfileIndex; }
    void setIESProfileIndex(int32_t idx) { m_IESProfileIndex = idx; }
    
    // Volumetrics
    bool getVolumetric() const { return m_Volumetric; }
    void setVolumetric(bool v) { m_Volumetric = v; }
    float getVolumetricAnisotropy() const { return m_VolumetricAnisotropy; }
    void setVolumetricAnisotropy(float g) { m_VolumetricAnisotropy = Math::Clamp(g, -0.99f, 0.99f); }
    
    // Calculate attenuation at distance
    float calculateAttenuation(float distance) const;
    
    // Light direction (for directional and spot lights)
    Math::Vector3 getDirection() const;
    
    // Light presets
    static Light* CreateDirectionalLight(const Math::Vector3& direction = Math::Vector3(0, -1, -1),
                                         const Math::Vector3& color = Math::Vector3(1, 1, 1),
                                         float intensity = 1.0f);
    
    static Light* CreatePointLight(const Math::Vector3& color = Math::Vector3(1, 1, 1),
                                   float intensity = 1.0f,
                                   float range = 10.0f);
    
    static Light* CreateSpotLight(float spotAngle = 45.0f,
                                  const Math::Vector3& color = Math::Vector3(1, 1, 1),
                                  float intensity = 1.0f,
                                  float range = 10.0f);
    
    // Lifecycle
    void OnCreate() override;
    void OnDestroy() override;
    
    // Main light (primary directional light)
    static Light* getMainLight() { return s_MainLight; }
    static void setMainLight(Light* light) { s_MainLight = light; }
    
    // Utility: convert from color temperature to RGB
    static Math::Vector3 KelvinToRGB(float temperatureK);
    
    // Utility: effective color with temperature tint applied
    Math::Vector3 getEffectiveColor() const;
    
private:
    Type m_Type;
    
    Math::Vector3 m_Color;
    float m_ColorTemperature;
    float m_Intensity;
    IntensityUnit m_IntensityUnit;
    FalloffModel m_FalloffModel;
    
    // Range
    float m_Range;
    
    // Spot light
    float m_SpotAngle;
    float m_InnerSpotAngle;
    
    // Area light
    Math::Vector2 m_AreaSize;
    float m_SourceRadius;
    float m_SourceLength;
    
    // Shadows
    bool m_CastShadows;
    bool m_SoftShadows;
    bool m_ContactShadows;
    float m_ShadowBias;
    float m_ShadowNormalBias;
    float m_Penumbra;
    float m_ShadowNearPlane;
    float m_ShadowFarPlane;
    ShadowQuality m_ShadowQuality;
    uint32_t m_ShadowMapResolution;
    uint8_t m_CascadeCount;
    std::array<float, 4> m_CascadeSplits;
    
    // Attenuation
    float m_ConstantAttenuation;
    float m_LinearAttenuation;
    float m_QuadraticAttenuation;
    
    // Cookies / IES
    int32_t m_CookieIndex;
    int32_t m_IESProfileIndex;
    
    // Volumetric
    bool m_Volumetric;
    float m_VolumetricAnisotropy;
    
    // Main light
    static Light* s_MainLight;
};

} // namespace Crescent
