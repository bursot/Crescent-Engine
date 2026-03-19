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

struct TextureLiveStats {
    size_t liveTextureCount = 0;
    uint64_t approximateBytes = 0;
};

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
    bool isEditableRGBA8() const;
    
    void setDimensions(uint32_t width, uint32_t height) { m_Width = width; m_Height = height; updateDebugRegistry(); }
    void setColorSpace(ColorSpace space) { m_ColorSpace = space; updateDebugRegistry(); }
    void setPath(const std::string& path) { m_Path = path; updateDebugRegistry(); }
    const std::string& getPath() const { return m_Path; }
    uint32_t getMipLevelCount() const { return m_MipLevelCount; }
    void setMipLevelCount(uint32_t mipLevelCount) { m_MipLevelCount = mipLevelCount; updateDebugRegistry(); }
    uint64_t getApproximateBytes() const { return m_ApproximateBytes; }
    void setApproximateBytes(uint64_t approximateBytes) { m_ApproximateBytes = approximateBytes; updateDebugRegistry(); }

    static TextureLiveStats getLiveStats();
    static void logLiveStats(const std::string& reason, size_t maxEntries = 8);
    
private:
    void updateDebugRegistry() const;

    MTL::Texture* m_Texture;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_MipLevelCount;
    uint64_t m_ApproximateBytes;
    ColorSpace m_ColorSpace;
    std::string m_Path;
};

// Loader/cache for textures using stb_image + Metal
class TextureLoader {
public:
    TextureLoader(MTL::Device* device, MTL::CommandQueue* commandQueue = nullptr);
    ~TextureLoader();
    
    std::shared_ptr<Texture2D> loadTexture(const std::string& path, bool srgb = true, bool flipVertical = true, bool normalMap = false);
    std::shared_ptr<Texture2D> loadEmbeddedCookedTexture(const std::string& cacheKey, bool srgb = true, bool normalMap = false);
    std::shared_ptr<Texture2D> loadTextureUncompressed(const std::string& path, bool srgb = true, bool flipVertical = true);
    std::shared_ptr<Texture2D> loadTextureFromMemory(const unsigned char* data, size_t size, bool srgb, bool flipVertical, const std::string& cacheKey, bool normalMap = false);
    std::shared_ptr<Texture2D> createTextureFromRGBA8(const std::string& cacheKey, const unsigned char* rgba, int width, int height, bool srgb, bool flipVertical, bool normalMap = false);
    bool updateTextureFromRGBA8(const std::shared_ptr<Texture2D>& texture,
                                const unsigned char* rgba,
                                int width,
                                int height,
                                bool flipVertical = false);
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

bool CookStaticLightmapToKTX2(const std::string& sourcePath, const std::string& outputPath);

} // namespace Crescent
