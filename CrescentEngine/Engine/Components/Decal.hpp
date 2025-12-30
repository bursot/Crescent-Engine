#pragma once

#include "../ECS/Component.hpp"
#include "../Math/Math.hpp"
#include "../Rendering/Texture.hpp"
#include <memory>
#include <string>

namespace Crescent {

class Decal : public Component {
public:
    Decal();
    virtual ~Decal() = default;

    COMPONENT_TYPE(Decal)

    const Math::Vector4& getTint() const { return m_Tint; }
    void setTint(const Math::Vector4& tint) { m_Tint = tint; }

    float getOpacity() const { return m_Opacity; }
    void setOpacity(float opacity) { m_Opacity = Math::Clamp(opacity, 0.0f, 1.0f); }

    float getEdgeSoftness() const { return m_EdgeSoftness; }
    void setEdgeSoftness(float softness) { m_EdgeSoftness = Math::Clamp(softness, 0.0f, 0.49f); }

    const Math::Vector2& getTiling() const { return m_Tiling; }
    void setTiling(const Math::Vector2& tiling) { m_Tiling = tiling; }

    const Math::Vector2& getOffset() const { return m_Offset; }
    void setOffset(const Math::Vector2& offset) { m_Offset = offset; }

    const std::shared_ptr<Texture2D>& getAlbedoTexture() const { return m_AlbedoTexture; }
    void setAlbedoTexture(const std::shared_ptr<Texture2D>& texture);
    const std::string& getAlbedoPath() const { return m_AlbedoPath; }
    void setAlbedoPath(const std::string& path) { m_AlbedoPath = path; }

    const std::shared_ptr<Texture2D>& getNormalTexture() const { return m_NormalTexture; }
    void setNormalTexture(const std::shared_ptr<Texture2D>& texture);
    const std::string& getNormalPath() const { return m_NormalPath; }
    void setNormalPath(const std::string& path) { m_NormalPath = path; }

    const std::shared_ptr<Texture2D>& getORMTexture() const { return m_ORMTexture; }
    void setORMTexture(const std::shared_ptr<Texture2D>& texture);
    const std::string& getORMPath() const { return m_ORMPath; }
    void setORMPath(const std::string& path) { m_ORMPath = path; }

    const std::shared_ptr<Texture2D>& getMaskTexture() const { return m_MaskTexture; }
    void setMaskTexture(const std::shared_ptr<Texture2D>& texture);
    const std::string& getMaskPath() const { return m_MaskPath; }
    void setMaskPath(const std::string& path) { m_MaskPath = path; }

private:
    Math::Vector4 m_Tint;
    float m_Opacity;
    float m_EdgeSoftness;
    Math::Vector2 m_Tiling;
    Math::Vector2 m_Offset;
    std::shared_ptr<Texture2D> m_AlbedoTexture;
    std::shared_ptr<Texture2D> m_NormalTexture;
    std::shared_ptr<Texture2D> m_ORMTexture;
    std::shared_ptr<Texture2D> m_MaskTexture;
    std::string m_AlbedoPath;
    std::string m_NormalPath;
    std::string m_ORMPath;
    std::string m_MaskPath;
};

} // namespace Crescent
