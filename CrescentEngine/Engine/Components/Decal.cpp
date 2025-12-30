#include "Decal.hpp"

namespace Crescent {

Decal::Decal()
    : m_Tint(1.0f, 1.0f, 1.0f, 1.0f)
    , m_Opacity(1.0f)
    , m_EdgeSoftness(0.05f)
    , m_Tiling(1.0f, 1.0f)
    , m_Offset(0.0f, 0.0f) {}

void Decal::setAlbedoTexture(const std::shared_ptr<Texture2D>& texture) {
    m_AlbedoTexture = texture;
    if (texture) {
        m_AlbedoPath = texture->getPath();
    } else {
        m_AlbedoPath.clear();
    }
}

void Decal::setNormalTexture(const std::shared_ptr<Texture2D>& texture) {
    m_NormalTexture = texture;
    if (texture) {
        m_NormalPath = texture->getPath();
    } else {
        m_NormalPath.clear();
    }
}

void Decal::setORMTexture(const std::shared_ptr<Texture2D>& texture) {
    m_ORMTexture = texture;
    if (texture) {
        m_ORMPath = texture->getPath();
    } else {
        m_ORMPath.clear();
    }
}

void Decal::setMaskTexture(const std::shared_ptr<Texture2D>& texture) {
    m_MaskTexture = texture;
    if (texture) {
        m_MaskPath = texture->getPath();
    } else {
        m_MaskPath.clear();
    }
}

} // namespace Crescent
