#define STB_IMAGE_IMPLEMENTATION
#include "Texture.hpp"
#include "stb_image.h"

// stb_image_write provides stbi_zlib_compress used by tinyexr when STB zlib is enabled
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Use stb's built-in zlib to avoid extra miniz linkage for EXR
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#include <Metal/Metal.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>
#include <vector>

namespace {

bool endsWithIgnoreCase(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

bool isEXRFile(const std::string& path) {
    return endsWithIgnoreCase(path, ".exr");
}

} // namespace

namespace Crescent {

Texture2D::Texture2D()
    : m_Texture(nullptr)
    , m_Width(0)
    , m_Height(0)
    , m_ColorSpace(ColorSpace::SRGB) {
}

Texture2D::~Texture2D() {
    if (m_Texture) {
        m_Texture->release();
        m_Texture = nullptr;
    }
}

void Texture2D::setHandle(MTL::Texture* texture) {
    if (m_Texture == texture) {
        return;
    }
    
    if (m_Texture) {
        m_Texture->release();
    }
    m_Texture = texture;
}

TextureLoader::TextureLoader(MTL::Device* device, MTL::CommandQueue* commandQueue)
    : m_Device(device)
    , m_CommandQueue(commandQueue) {
}

TextureLoader::~TextureLoader() {
    m_Cache.clear();
}

std::shared_ptr<Texture2D> TextureLoader::loadTexture(const std::string& path, bool srgb, bool flipVertical) {
    if (!m_Device) {
        std::cerr << "[TextureLoader] Invalid Metal device, cannot load texture\n";
        return nullptr;
    }
    
    // Cache lookup
    if (auto it = m_Cache.find(path); it != m_Cache.end()) {
        if (auto cached = it->second.lock()) {
            return cached;
        }
    }

    if (isEXRFile(path)) {
        return loadEXRTexture(path, flipVertical);
    }

    // HDR branch
    if (stbi_is_hdr(path.c_str())) {
        return loadHDRTexture(path, flipVertical);
    }
    
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    
    if (!data) {
        std::cerr << "[TextureLoader] Failed to load texture: " << path 
                  << " reason: " << stbi_failure_reason() << std::endl;
        return nullptr;
    }
    
    MTL::PixelFormat format = srgb ? MTL::PixelFormatRGBA8Unorm_sRGB : MTL::PixelFormatRGBA8Unorm;
    
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setWidth(static_cast<NS::UInteger>(width));
    desc->setHeight(static_cast<NS::UInteger>(height));
    desc->setPixelFormat(format);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(mipLevels));
    
    MTL::Texture* texture = m_Device->newTexture(desc);
    desc->release();
    
    if (!texture) {
        std::cerr << "[TextureLoader] Failed to create Metal texture for: " << path << std::endl;
        stbi_image_free(data);
        return nullptr;
    }
    
    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    texture->replaceRegion(region, 0, data, static_cast<NS::UInteger>(width * 4));
    stbi_image_free(data);

    // Generate mipmaps for smoother environment blur + texture sampling
    generateMipmaps(texture);
    
    auto tex = std::make_shared<Texture2D>();
    tex->setHandle(texture); // Texture owns the Metal resource
    tex->setDimensions(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    tex->setColorSpace(srgb ? Texture2D::ColorSpace::SRGB : Texture2D::ColorSpace::Linear);
    tex->setPath(path);
    
    m_Cache[path] = tex;
    return tex;
}

std::shared_ptr<Texture2D> TextureLoader::loadTextureFromMemory(const unsigned char* data, size_t size, bool srgb, bool flipVertical, const std::string& cacheKey) {
    if (!data || size == 0) {
        return nullptr;
    }
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
    stbi_uc* decoded = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, STBI_rgb_alpha);
    if (!decoded) {
        std::cerr << "[TextureLoader] Failed to decode embedded texture: " << stbi_failure_reason() << std::endl;
        return nullptr;
    }
    auto tex = createTextureFromRGBA8(cacheKey, decoded, width, height, srgb, false);
    stbi_image_free(decoded);
    return tex;
}

std::shared_ptr<Texture2D> TextureLoader::createTextureFromRGBA8(const std::string& cacheKey, const unsigned char* rgba, int width, int height, bool srgb, bool flipVertical) {
    if (!m_Device || !rgba || width <= 0 || height <= 0) {
        return nullptr;
    }
    
    if (!cacheKey.empty()) {
        if (auto it = m_Cache.find(cacheKey); it != m_Cache.end()) {
            if (auto cached = it->second.lock()) {
                return cached;
            }
        }
    }
    
    const unsigned char* uploadData = rgba;
    std::vector<unsigned char> flipped;
    if (flipVertical && height > 1) {
        size_t rowBytes = static_cast<size_t>(width) * 4;
        flipped.resize(static_cast<size_t>(height) * rowBytes);
        for (int y = 0; y < height; ++y) {
            std::memcpy(
                flipped.data() + rowBytes * static_cast<size_t>(y),
                rgba + rowBytes * static_cast<size_t>(height - 1 - y),
                rowBytes
            );
        }
        uploadData = flipped.data();
    }
    
    MTL::PixelFormat format = srgb ? MTL::PixelFormatRGBA8Unorm_sRGB : MTL::PixelFormatRGBA8Unorm;
    
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setWidth(static_cast<NS::UInteger>(width));
    desc->setHeight(static_cast<NS::UInteger>(height));
    desc->setPixelFormat(format);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(mipLevels));
    
    MTL::Texture* texture = m_Device->newTexture(desc);
    desc->release();
    
    if (!texture) {
        std::cerr << "[TextureLoader] Failed to create Metal texture from memory" << std::endl;
        return nullptr;
    }
    
    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    texture->replaceRegion(region, 0, uploadData, static_cast<NS::UInteger>(width * 4));
    
    generateMipmaps(texture);
    
    auto tex = std::make_shared<Texture2D>();
    tex->setHandle(texture);
    tex->setDimensions(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    tex->setColorSpace(srgb ? Texture2D::ColorSpace::SRGB : Texture2D::ColorSpace::Linear);
    if (!cacheKey.empty()) {
        tex->setPath(cacheKey);
        m_Cache[cacheKey] = tex;
    }
    return tex;
}

std::shared_ptr<Texture2D> TextureLoader::loadEXRTexture(const std::string& path, bool flipVertical) {
    if (!m_Device) {
        return nullptr;
    }

    // Cache lookup (EXR textures share same cache)
    if (auto it = m_Cache.find(path); it != m_Cache.end()) {
        if (auto cached = it->second.lock()) {
            return cached;
        }
    }

    const char* err = nullptr;
    float* imageData = nullptr;
    int width = 0;
    int height = 0;
    int ret = LoadEXR(&imageData, &width, &height, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS || !imageData) {
        std::cerr << "[TextureLoader] Failed to load EXR texture: " << path;
        if (err) {
            std::cerr << " reason: " << err;
            FreeEXRErrorMessage(err);
        }
        std::cerr << std::endl;
        return nullptr;
    }
    if (err) {
        FreeEXRErrorMessage(err);
    }

    // TinyEXR returns RGBA float (alpha=1 if absent)
    std::vector<float> flipped;
    float* uploadData = imageData;
    const size_t rowStride = static_cast<size_t>(width) * 4;
    if (flipVertical && height > 1) {
        flipped.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        for (int y = 0; y < height; ++y) {
            std::memcpy(
                flipped.data() + rowStride * static_cast<size_t>(y),
                imageData + rowStride * static_cast<size_t>(height - 1 - y),
                rowStride * sizeof(float)
            );
        }
        uploadData = flipped.data();
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setWidth(static_cast<NS::UInteger>(width));
    desc->setHeight(static_cast<NS::UInteger>(height));
    desc->setPixelFormat(MTL::PixelFormatRGBA32Float);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(mipLevels));

    MTL::Texture* texture = m_Device->newTexture(desc);
    desc->release();

    if (!texture) {
        std::cerr << "[TextureLoader] Failed to create Metal texture for EXR: " << path << std::endl;
        std::free(imageData);
        return nullptr;
    }

    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    texture->replaceRegion(region, 0, uploadData, static_cast<NS::UInteger>(rowStride * sizeof(float)));
    std::free(imageData);

    // Generate mip chain for smooth sampling
    generateMipmaps(texture);

    auto tex = std::make_shared<Texture2D>();
    tex->setHandle(texture);
    tex->setDimensions(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    tex->setColorSpace(Texture2D::ColorSpace::Linear); // EXR is always linear
    tex->setPath(path);

    m_Cache[path] = tex;
    return tex;
}

std::shared_ptr<Texture2D> TextureLoader::loadHDRTexture(const std::string& path, bool flipVertical) {
    if (!m_Device) {
        return nullptr;
    }
    
    // Cache lookup (HDR textures share same cache as LDR)
    if (auto it = m_Cache.find(path); it != m_Cache.end()) {
        if (auto cached = it->second.lock()) {
            return cached;
        }
    }
    
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
    float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "[TextureLoader] Failed to load HDR texture: " << path 
                  << " reason: " << stbi_failure_reason() << std::endl;
        return nullptr;
    }
    
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setWidth(static_cast<NS::UInteger>(width));
    desc->setHeight(static_cast<NS::UInteger>(height));
    desc->setPixelFormat(MTL::PixelFormatRGBA32Float);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(mipLevels));
    
    MTL::Texture* texture = m_Device->newTexture(desc);
    desc->release();
    
    if (!texture) {
        std::cerr << "[TextureLoader] Failed to create Metal texture for HDR: " << path << std::endl;
        stbi_image_free(data);
        return nullptr;
    }
    
    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    texture->replaceRegion(region, 0, data, static_cast<NS::UInteger>(width * 4 * sizeof(float)));
    stbi_image_free(data);
    
    // Generate mip chain to allow blurred sampling
    generateMipmaps(texture);
    
    auto tex = std::make_shared<Texture2D>();
    tex->setHandle(texture);
    tex->setDimensions(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    tex->setColorSpace(Texture2D::ColorSpace::Linear);
    tex->setPath(path);
    
    m_Cache[path] = tex;
    return tex;
}

void TextureLoader::generateMipmaps(MTL::Texture* texture) {
    if (!texture || texture->mipmapLevelCount() <= 1 || !m_CommandQueue) {
        return;
    }
    
    MTL::CommandBuffer* commandBuffer = m_CommandQueue->commandBuffer();
    if (!commandBuffer) {
        return;
    }
    
    MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
    if (blit) {
        blit->generateMipmaps(texture);
        blit->endEncoding();
    }
    
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();
}

std::shared_ptr<Texture2D> TextureLoader::createSolidTexture(float r, float g, float b, float a, bool srgb) {
    if (!m_Device) return nullptr;
    
    const uint32_t width = 1;
    const uint32_t height = 1;
    auto toByte = [](float v) {
        return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    
    uint8_t pixel[4] = { toByte(r), toByte(g), toByte(b), toByte(a) };
    
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setWidth(width);
    desc->setHeight(height);
    desc->setPixelFormat(srgb ? MTL::PixelFormatRGBA8Unorm_sRGB : MTL::PixelFormatRGBA8Unorm);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    uint32_t mipLevels = 1;
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(mipLevels));
    
    MTL::Texture* texture = m_Device->newTexture(desc);
    desc->release();
    
    if (!texture) {
        std::cerr << "[TextureLoader] Failed to create solid color texture\n";
        return nullptr;
    }
    
    MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
    texture->replaceRegion(region, 0, pixel, 4);
    generateMipmaps(texture);
    
    auto tex = std::make_shared<Texture2D>();
    tex->setHandle(texture);
    tex->setDimensions(width, height);
    tex->setColorSpace(srgb ? Texture2D::ColorSpace::SRGB : Texture2D::ColorSpace::Linear);
    tex->setPath("builtin://solid");
    
    return tex;
}

std::shared_ptr<Texture2D> TextureLoader::createFlatNormalTexture() {
    // Flat normal in tangent space = (0.5, 0.5, 1.0)
    return createSolidTexture(0.5f, 0.5f, 1.0f, 1.0f, false);
}

} // namespace Crescent
