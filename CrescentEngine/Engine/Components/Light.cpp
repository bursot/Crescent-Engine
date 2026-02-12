#include "Light.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <cmath>

namespace Crescent {

Light* Light::s_MainLight = nullptr;

Light::Light()
    : m_Type(Type::Directional)
    , m_Color(1.0f, 1.0f, 1.0f)
    , m_ColorTemperature(6500.0f)
    , m_Intensity(1.0f)
    , m_IntensityUnit(IntensityUnit::Unitless)
    , m_FalloffModel(FalloffModel::InverseSquare)
    , m_Range(10.0f)
    , m_SpotAngle(45.0f)
    , m_InnerSpotAngle(30.0f)
    , m_AreaSize(1.0f, 1.0f)
    , m_SourceRadius(0.0f)
    , m_SourceLength(0.0f)
    , m_CastShadows(true)
    , m_SoftShadows(true)
    , m_ContactShadows(false)
    , m_ShadowBias(0.0005f)
    , m_ShadowNormalBias(0.001f)
    , m_Penumbra(1.0f)
    , m_ShadowNearPlane(0.1f)
    , m_ShadowFarPlane(200.0f)
    , m_ShadowQuality(ShadowQuality::High)
    , m_ShadowMapResolution(1024)
    , m_CascadeCount(4)
    , m_ConstantAttenuation(1.0f)
    , m_LinearAttenuation(0.09f)
    , m_QuadraticAttenuation(0.032f)
    , m_CookieIndex(-1)
    , m_IESProfileIndex(-1)
    , m_Volumetric(false)
    , m_VolumetricAnisotropy(0.0f) {
        
    // Practical split distribution: keeps near shadows sharper without starving far coverage.
    m_CascadeSplits = {0.08f, 0.22f, 0.5f, 1.0f};
}

float Light::calculateAttenuation(float distance) const {
    if (m_Type == Type::Directional) {
        return 1.0f; // No attenuation for directional lights
    }
    
    if (distance > m_Range) {
        return 0.0f;
    }
    
    float attenuation = 1.0f;
    if (m_FalloffModel == FalloffModel::InverseSquare) {
        // Physically plausible falloff with soft clamp near zero
        float denom = m_ConstantAttenuation + 
                      m_LinearAttenuation * std::max(distance, 0.001f) + 
                      m_QuadraticAttenuation * distance * distance;
        attenuation = (denom > Math::EPSILON) ? 1.0f / denom : 0.0f;
    } else {
        attenuation = Math::Clamp(1.0f - (distance / m_Range), 0.0f, 1.0f);
    }
    
    return attenuation;
}

Math::Vector3 Light::getDirection() const {
    if (!m_Entity) {
        return Math::Vector3::Down;
    }
    
    Transform* transform = m_Entity->getTransform();
    return transform->forward();
}

Light* Light::CreateDirectionalLight(const Math::Vector3& direction, 
                                      const Math::Vector3& color, 
                                      float intensity) {
    Light* light = new Light();
    light->setType(Type::Directional);
    light->setColor(color);
    light->setIntensity(intensity);
    
    return light;
}

Light* Light::CreatePointLight(const Math::Vector3& color, 
                                float intensity, 
                                float range) {
    Light* light = new Light();
    light->setType(Type::Point);
    light->setColor(color);
    light->setIntensity(intensity);
    light->setRange(range);
    
    return light;
}

Light* Light::CreateSpotLight(float spotAngle, 
                                      const Math::Vector3& color, 
                                      float intensity, 
                                      float range) {
    Light* light = new Light();
    light->setType(Type::Spot);
    light->setSpotAngle(spotAngle);
    light->setInnerSpotAngle(spotAngle * 0.7f);
    light->setColor(color);
    light->setIntensity(intensity);
    light->setRange(range);
    
    return light;
}

void Light::OnCreate() {
    // If this is a directional light and no main light is set, make it the main light
    if (m_Type == Type::Directional && s_MainLight == nullptr) {
        s_MainLight = this;
    }
}

void Light::OnDestroy() {
    // If this was the main light, clear it
    if (s_MainLight == this) {
        s_MainLight = nullptr;
    }
}

void Light::setColorTemperature(float kelvin) {
    m_ColorTemperature = Math::Clamp(kelvin, 1000.0f, 25000.0f);
    m_Color = KelvinToRGB(m_ColorTemperature);
}

void Light::setAreaSize(const Math::Vector2& size) {
    m_AreaSize = Math::Vector2(
        std::max(0.0f, size.x),
        std::max(0.0f, size.y)
    );
}

void Light::setCascadeCount(uint8_t cascades) {
    m_CascadeCount = std::max<uint8_t>(1, std::min<uint8_t>(4, cascades));
}

void Light::setCascadeSplits(const std::array<float, 4>& splits) {
    m_CascadeSplits = splits;
    // Ensure monotonically increasing and clamped to [0,1]
    for (size_t i = 0; i < m_CascadeSplits.size(); ++i) {
        float prev = (i == 0) ? 0.0f : m_CascadeSplits[i - 1];
        m_CascadeSplits[i] = Math::Clamp(std::max(prev, m_CascadeSplits[i]), 0.0f, 1.0f);
    }
}

void Light::setShadowRange(float nearPlane, float farPlane) {
    m_ShadowNearPlane = std::max(0.001f, nearPlane);
    m_ShadowFarPlane = std::max(m_ShadowNearPlane + 0.1f, farPlane);
}

Math::Vector3 Light::KelvinToRGB(float temperatureK) {
    float t = temperatureK / 100.0f;
    float r, g, b;
    
    // Approximation based on Tanner Helland's algorithm
    if (t <= 66.0f) {
        r = 255.0f;
        g = 99.4708025861f * std::log(t) - 161.1195681661f;
        b = (t <= 19.0f) ? 0.0f : 138.5177312231f * std::log(t - 10.0f) - 305.0447927307f;
    } else {
        r = 329.698727446f * std::pow(t - 60.0f, -0.1332047592f);
        g = 288.1221695283f * std::pow(t - 60.0f, -0.0755148492f);
        b = 255.0f;
    }
    
    auto clamp01 = [](float v) {
        return Math::Clamp(v / 255.0f, 0.0f, 1.0f);
    };
    
    return Math::Vector3(clamp01(r), clamp01(g), clamp01(b));
}

Math::Vector3 Light::getEffectiveColor() const {
    return m_Color; // temperature baked into color when setColorTemperature called
}

} // namespace Crescent
