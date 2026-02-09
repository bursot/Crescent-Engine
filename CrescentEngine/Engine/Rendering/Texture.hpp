#pragma once

#include <memory>
#include <string>
#include <unordered_map>

// Forward declare Metal types to avoid pulling metal-cpp into headers
namespace MTL {
    class Texture;
    class Device;
    class CommandQueue;
}

namespace Crescent {

// Simple 2D texture wrapper that owns a Metal texture handle
class Texture2D {
public:
    enum class ColorSpace {
        Linear,
        SRGB
    };
    
    Texture2D();
    ~Texture2D();
    
    // Metal texture handle
    MTL::Texture* getHandle() const { return m_Texture; }
    void setHandle(MTL::Texture* texture);
    
    // Metadata
    uint32_t getWidth() const { return m_Width; }
    uint32_t getHeight() const { return m_Height; }
    bool isSRGB() const { return m_ColorSpace == ColorSpace::SRGB; }
    
    void setDimensions(uint32_t width, uint32_t height) { m_Width = width; m_Height = height; }
    void setColorSpace(ColorSpace space) { m_ColorSpace = space; }
    void setPath(const std::string& path) { m_Path = path; }
    const std::string& getPath() const { return m_Path; }
    
private:
    MTL::Texture* m_Texture;
    uint32_t m_Width;
    uint32_t m_Height;
    ColorSpace m_ColorSpace;
    std::string m_Path;
};

// Loader/cache for textures using stb_image + Metal
class TextureLoader {
public:
    TextureLoader(MTL::Device* device, MTL::CommandQueue* commandQueue = nullptr);
    ~TextureLoader();
    
    std::shared_ptr<Texture2D> loadTexture(const std::string& path, bool srgb = true, bool flipVertical = true, bool normalMap = false);
    std::shared_ptr<Texture2D> loadTextureFromMemory(const unsigned char* data, size_t size, bool srgb, bool flipVertical, const std::string& cacheKey, bool normalMap = false);
    std::shared_ptr<Texture2D> createTextureFromRGBA8(const std::string& cacheKey, const unsigned char* rgba, int width, int height, bool srgb, bool flipVertical, bool normalMap = false);
    void invalidateTexture(const std::string& path);
    
    // Utility textures for defaults/fallbacks
    std::shared_ptr<Texture2D> createSolidTexture(float r, float g, float b, float a = 1.0f, bool srgb = true);
    std::shared_ptr<Texture2D> createFlatNormalTexture();
    
private:
    std::shared_ptr<Texture2D> loadHDRTexture(const std::string& path, bool flipVertical);
    std::shared_ptr<Texture2D> loadEXRTexture(const std::string& path, bool flipVertical);
    std::shared_ptr<Texture2D> loadKTX2Texture(const std::string& path, bool srgb, bool normalMap, const std::string& cacheKey);
    void generateMipmaps(MTL::Texture* texture);
    
    MTL::Device* m_Device;
    MTL::CommandQueue* m_CommandQueue;
    std::unordered_map<std::string, std::weak_ptr<Texture2D>> m_Cache;
};

} // namespace Crescent
