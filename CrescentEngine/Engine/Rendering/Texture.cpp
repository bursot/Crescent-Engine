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

#include "../Assets/AssetDatabase.hpp"
#include <filesystem>
#include <Metal/Metal.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h>
#include <iomanip>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>
#include <iostream>
#include <vector>

#define BASISD_SUPPORT_KTX2 1
#define BASISD_SUPPORT_KTX2_ZSTD 0
#include "../../../ThirdParty/basisu/transcoder/basisu_transcoder.h"

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

bool isKTX2File(const std::string& path) {
    return endsWithIgnoreCase(path, ".ktx2");
}

bool isLdrTextureFile(const std::string& path) {
    return endsWithIgnoreCase(path, ".png") ||
           endsWithIgnoreCase(path, ".jpg") ||
           endsWithIgnoreCase(path, ".jpeg") ||
           endsWithIgnoreCase(path, ".tga") ||
           endsWithIgnoreCase(path, ".bmp") ||
           endsWithIgnoreCase(path, ".gif") ||
           endsWithIgnoreCase(path, ".tif") ||
           endsWithIgnoreCase(path, ".tiff");
}

bool isKtx2Disabled() {
    const char* flag = std::getenv("CRESCENT_DISABLE_KTX2");
    return flag && *flag && std::string(flag) != "0";
}

bool isKtx2DebugEnabled() {
    const char* flag = std::getenv("CRESCENT_KTX2_DEBUG");
    return flag && *flag && std::string(flag) != "0";
}

bool isTextureMemDebugEnabled() {
    const char* flag = std::getenv("CRESCENT_TEX_MEM_DEBUG");
    return flag && *flag && std::string(flag) != "0";
}

std::string QuoteShellArg(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::string GetExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) {
        return "";
    }
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return "";
    }
    if (!buffer.empty() && buffer.back() == '\0') {
        buffer.pop_back();
    }
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(buffer, ec);
    return ec ? buffer : resolved.string();
}

std::filesystem::path ResolveBasisuPath() {
    const char* envPath = std::getenv("CRESCENT_BASISU_PATH");
    if (envPath && *envPath) {
        return envPath;
    }

    std::string exePath = GetExecutablePath();
    if (!exePath.empty()) {
        std::filesystem::path bundlePath = std::filesystem::path(exePath).parent_path() / ".." / "Resources" / "basisu";
        std::error_code ec;
        if (std::filesystem::exists(bundlePath, ec)) {
            return bundlePath;
        }
    }

    return std::filesystem::current_path() / "ThirdParty" / "basisu" / "basisu";
}

} // namespace

namespace Crescent {

namespace {

std::once_flag g_basisuInitFlag;

void EnsureBasisuInitialized() {
    std::call_once(g_basisuInitFlag, []() {
        basist::basisu_transcoder_init();
    });
}

basist::transcoder_texture_format SelectAstcFormat(bool normalMap) {
    (void)normalMap;
    // BasisU KTX2 transcoder only supports ASTC LDR 4x4 for ETC1S/UASTC.
    return basist::transcoder_texture_format::cTFASTC_LDR_4x4_RGBA;
}

MTL::PixelFormat SelectAstcPixelFormat(bool srgb, bool normalMap) {
    (void)normalMap;
    return srgb ? MTL::PixelFormatASTC_4x4_sRGB : MTL::PixelFormatASTC_4x4_LDR;
}

uint32_t ComputeBlocksX(uint32_t width, uint32_t blockWidth) {
    return (width + blockWidth - 1) / blockWidth;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& outData) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    outData.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(outData.data()), size));
}

std::string HashPathStable(const std::string& input) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : input) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

uint32_t ComputeMipLevels(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return 1;
    }
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1u, width >> 1);
        height = std::max(1u, height >> 1);
        ++levels;
    }
    return levels;
}

uint64_t ComputeRGBA8MipChainBytes(uint32_t width, uint32_t height) {
    uint64_t total = 0;
    uint32_t levels = ComputeMipLevels(width, height);
    for (uint32_t level = 0; level < levels; ++level) {
        uint32_t w = std::max(1u, width >> level);
        uint32_t h = std::max(1u, height >> level);
        total += static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 4u;
    }
    return total;
}

void LogTextureMemory(const std::string& label, uint32_t width, uint32_t height, uint32_t levels, uint64_t bytes) {
    if (!isTextureMemDebugEnabled()) {
        return;
    }
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::cerr << "[TextureMem] " << label
              << " " << width << "x" << height
              << " mips=" << levels
              << " bytes=" << bytes
              << " (~" << std::fixed << std::setprecision(2) << mb << " MB)"
              << std::endl;
}

std::string GetKtx2CachePath(const std::string& sourcePath) {
    AssetDatabase& db = AssetDatabase::getInstance();
    std::string libraryPath = db.getLibraryPath();
    if (libraryPath.empty()) {
        if (isKtx2DebugEnabled()) {
            std::cerr << "[TextureLoader] KTX2 debug: LibraryPath empty, cannot cache " << sourcePath << std::endl;
        }
        return "";
    }
    std::string guid = db.getGuidForPath(sourcePath);
    if (guid.empty()) {
        guid = db.registerAsset(sourcePath);
    }
    if (guid.empty()) {
        std::error_code ec;
        std::filesystem::path absPath = std::filesystem::weakly_canonical(sourcePath, ec);
        std::string key = ec ? sourcePath : absPath.string();
        guid = "path_" + HashPathStable(key);
        if (isKtx2DebugEnabled()) {
            std::cerr << "[TextureLoader] KTX2 debug: GUID missing, using hashed key for " << sourcePath << std::endl;
        }
    }
    std::filesystem::path cacheDir = std::filesystem::path(libraryPath) / "ImportCache";
    return (cacheDir / (guid + ".ktx2")).string();
}

bool IsCacheValid(const std::string& sourcePath, const std::string& cachePath) {
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec)) {
        return false;
    }
    auto cacheTime = std::filesystem::last_write_time(cachePath, ec);
    if (ec) {
        return true;
    }
    auto sourceTime = std::filesystem::last_write_time(sourcePath, ec);
    if (ec) {
        return true;
    }
    return cacheTime >= sourceTime;
}

bool EncodeKtx2WithBasisuCLI(const std::string& sourcePath,
                             const std::string& outputPath,
                             bool srgb,
                             bool normalMap,
                             bool flipVertical,
                             bool generateMips) {
    std::filesystem::path basisuPath = ResolveBasisuPath();
    std::error_code ec;
    if (!std::filesystem::exists(basisuPath, ec)) {
        std::cerr << "[TextureLoader] BasisU CLI not found: " << basisuPath << std::endl;
        return false;
    }

    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path(), ec);

    std::string cmd;
    cmd.reserve(512);
    cmd += QuoteShellArg(basisuPath.string());
    cmd += " -ktx2 -ktx2_no_zstandard";
    cmd += normalMap ? " -uastc" : " -etc1s";
    cmd += srgb ? " -srgb" : " -linear";
    if (flipVertical) {
        cmd += " -y_flip";
    }
    if (generateMips) {
        cmd += " -mipmap";
    }
    cmd += " -quality ";
    cmd += normalMap ? "50" : "80";
    cmd += " -effort ";
    cmd += normalMap ? "4" : "4";
    cmd += " -file ";
    cmd += QuoteShellArg(sourcePath);
    cmd += " -output_file ";
    cmd += QuoteShellArg(outputPath);
    cmd += " -no_status_output";

    if (isKtx2DebugEnabled()) {
        std::cerr << "[TextureLoader] KTX2 debug: Encode " << sourcePath << " -> " << outputPath << std::endl;
    }

    int result = std::system(cmd.c_str());
    if (result != 0) {
        std::cerr << "[TextureLoader] BasisU encode failed (" << result << "): " << sourcePath << std::endl;
    }
    return result == 0;
}

} // namespace

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

void TextureLoader::invalidateTexture(const std::string& path) {
    if (path.empty()) {
        return;
    }
    m_Cache.erase(path);
}

std::shared_ptr<Texture2D> TextureLoader::loadTexture(const std::string& path, bool srgb, bool flipVertical, bool normalMap) {
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

    if (!isKtx2Disabled() && isKTX2File(path)) {
        if (isKtx2DebugEnabled()) {
            std::cerr << "[TextureLoader] KTX2 debug: Loading KTX2 source " << path << std::endl;
        }
        auto tex = loadKTX2Texture(path, srgb, normalMap, path);
        if (tex) {
            m_Cache[path] = tex;
        }
        return tex;
    }

    if (!isKtx2Disabled() && isLdrTextureFile(path)) {
        std::string cachePath = GetKtx2CachePath(path);
        if (isKtx2DebugEnabled()) {
            std::cerr << "[TextureLoader] KTX2 debug: Cache path for " << path << " = " << cachePath << std::endl;
        }
        if (!cachePath.empty()) {
            if (IsCacheValid(path, cachePath)) {
                if (isKtx2DebugEnabled()) {
                    std::cerr << "[TextureLoader] KTX2 debug: Using cached KTX2 " << cachePath << std::endl;
                }
                auto tex = loadKTX2Texture(cachePath, srgb, normalMap, path);
                if (tex) {
                    m_Cache[path] = tex;
                    return tex;
                }
            }

            bool generated = EncodeKtx2WithBasisuCLI(path, cachePath, srgb, normalMap, flipVertical, true);
            if (isKtx2DebugEnabled()) {
                std::cerr << "[TextureLoader] KTX2 debug: Encode result for " << path << " = " << (generated ? "ok" : "fail") << std::endl;
            }
            if (generated) {
                auto tex = loadKTX2Texture(cachePath, srgb, normalMap, path);
                if (tex) {
                    m_Cache[path] = tex;
                    return tex;
                }
            }
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
    LogTextureMemory("RGBA8", static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                     ComputeMipLevels(static_cast<uint32_t>(width), static_cast<uint32_t>(height)),
                     ComputeRGBA8MipChainBytes(static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
    
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

std::shared_ptr<Texture2D> TextureLoader::loadKTX2Texture(const std::string& path,
                                                          bool srgb,
                                                          bool normalMap,
                                                          const std::string& cacheKey) {
    if (!m_Device) {
        return nullptr;
    }

    std::vector<uint8_t> fileData;
    if (!ReadFileBytes(path, fileData)) {
        std::cerr << "[TextureLoader] Failed to read KTX2 file: " << path << std::endl;
        return nullptr;
    }

    EnsureBasisuInitialized();

    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(fileData.data(), static_cast<uint32_t>(fileData.size()))) {
        std::cerr << "[TextureLoader] Invalid KTX2 file: " << path << std::endl;
        return nullptr;
    }
    if (!transcoder.start_transcoding()) {
        std::cerr << "[TextureLoader] KTX2 transcoder init failed: " << path << std::endl;
        return nullptr;
    }

    if (transcoder.get_layers() > 1 || transcoder.get_faces() > 1) {
        std::cerr << "[TextureLoader] KTX2 arrays/cubemaps not supported yet: " << path << std::endl;
        return nullptr;
    }
    if (transcoder.is_hdr()) {
        std::cerr << "[TextureLoader] KTX2 HDR textures not supported yet: " << path << std::endl;
        return nullptr;
    }

    uint32_t width = transcoder.get_width();
    uint32_t height = transcoder.get_height();
    uint32_t levels = std::max(1u, transcoder.get_levels());

    basist::transcoder_texture_format fmt = SelectAstcFormat(normalMap);
    MTL::PixelFormat pixelFormat = SelectAstcPixelFormat(srgb, normalMap);

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setWidth(static_cast<NS::UInteger>(width));
    desc->setHeight(static_cast<NS::UInteger>(height));
    desc->setPixelFormat(pixelFormat);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(levels));

    MTL::Texture* texture = m_Device->newTexture(desc);
    desc->release();
    if (!texture) {
        std::cerr << "[TextureLoader] Failed to create KTX2 Metal texture: " << path << std::endl;
        return nullptr;
    }

    const uint32_t blockWidth = basist::basis_get_block_width(fmt);
    const uint32_t blockHeight = basist::basis_get_block_height(fmt);
    const uint32_t bytesPerBlock = basist::basis_get_bytes_per_block_or_pixel(fmt);

    std::vector<uint8_t> levelData;
    uint64_t totalBytes = 0;
    for (uint32_t level = 0; level < levels; ++level) {
        basist::ktx2_image_level_info levelInfo{};
        if (!transcoder.get_image_level_info(levelInfo, level, 0, 0)) {
            std::cerr << "[TextureLoader] Failed to query KTX2 level info for level " << level << ": " << path << std::endl;
            texture->release();
            return nullptr;
        }

        uint32_t levelWidth = std::max(1u, levelInfo.m_orig_width);
        uint32_t levelHeight = std::max(1u, levelInfo.m_orig_height);
        uint32_t blocksX = ComputeBlocksX(levelWidth, blockWidth);
        uint32_t blocksY = ComputeBlocksX(levelHeight, blockHeight);
        uint32_t totalBlocks = std::max(1u, blocksX * blocksY);
        uint32_t dataSize = basist::basis_compute_transcoded_image_size_in_bytes(fmt, levelWidth, levelHeight);
        uint32_t expectedSize = totalBlocks * bytesPerBlock;
        if (dataSize < expectedSize) {
            dataSize = expectedSize;
        }
        uint32_t bufferBlocks = dataSize / bytesPerBlock;
        if (bufferBlocks < totalBlocks) {
            bufferBlocks = totalBlocks;
            dataSize = bufferBlocks * bytesPerBlock;
        }
        levelData.resize(dataSize);
        totalBytes += static_cast<uint64_t>(dataSize);

        if (!transcoder.transcode_image_level(level, 0, 0,
                                              levelData.data(), bufferBlocks, fmt,
                                              0, blocksX)) {
            std::cerr << "[TextureLoader] KTX2 transcode failed for level " << level << ": " << path << std::endl;
            texture->release();
            return nullptr;
        }

        uint32_t bytesPerRow = blocksX * bytesPerBlock;
        MTL::Region region = MTL::Region::Make2D(0, 0,
                                                 static_cast<NS::UInteger>(levelWidth),
                                                 static_cast<NS::UInteger>(levelHeight));
        texture->replaceRegion(region, static_cast<NS::UInteger>(level),
                               levelData.data(),
                               static_cast<NS::UInteger>(bytesPerRow));
    }
    LogTextureMemory("KTX2/ASTC4x4", width, height, levels, totalBytes);

    auto tex = std::make_shared<Texture2D>();
    tex->setHandle(texture);
    tex->setDimensions(width, height);
    tex->setColorSpace(srgb ? Texture2D::ColorSpace::SRGB : Texture2D::ColorSpace::Linear);
    tex->setPath(cacheKey.empty() ? path : cacheKey);

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
    LogTextureMemory("SolidRGBA8", width, height, 1, 4);
    
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

// Include BasisU transcoder implementation directly to avoid build system changes.
#include "../../../ThirdParty/basisu/transcoder/basisu_transcoder.cpp"
