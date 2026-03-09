#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../CrescentEngine/Engine/Rendering/stb_image_write.h"

namespace {

constexpr const char* kEmbeddedTextureMarker = "#embedded:";

std::string hashPathStable(const std::string& input) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : input) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

std::string canonicalizePathIfPossible(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : canonical.string();
}

std::string trimFormatHint(const char* hint) {
    if (!hint) {
        return "";
    }
    std::string out(hint);
    out.erase(std::find(out.begin(), out.end(), '\0'), out.end());
    out.erase(std::remove_if(out.begin(), out.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }), out.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string normalizedCacheKey(const std::string& modelPath,
                               const std::string& assetRoot,
                               int index,
                               const aiTexture* texture) {
    std::string sourcePath = canonicalizePathIfPossible(modelPath);
    std::string normalizedSource = sourcePath;
    if (!assetRoot.empty()) {
        std::error_code ec;
        std::filesystem::path relative = std::filesystem::relative(sourcePath, canonicalizePathIfPossible(assetRoot), ec);
        if (!ec && !relative.empty()) {
            normalizedSource = relative.generic_string();
        }
    }
    std::string key = normalizedSource + kEmbeddedTextureMarker + std::to_string(index);
    std::string hint = texture ? trimFormatHint(texture->achFormatHint) : "";
    if (!hint.empty()) {
        key += ".";
        key += hint;
    }
    return key;
}

std::string outputExtensionForTexture(const aiTexture* texture) {
    std::string hint = texture ? trimFormatHint(texture->achFormatHint) : "";
    if (hint == "jpeg") {
        hint = "jpg";
    }
    if (texture && texture->mHeight > 0) {
        return ".png";
    }
    if (hint == "png" || hint == "jpg" || hint == "jpeg" || hint == "bmp" || hint == "tga") {
        return "." + hint;
    }
    return "";
}

bool buildEmbeddedRGBA(const aiTexture* texture, std::vector<unsigned char>& rgba) {
    if (!texture || texture->mHeight == 0 || !texture->pcData || texture->mWidth == 0) {
        return false;
    }
    size_t texelCount = static_cast<size_t>(texture->mWidth) * static_cast<size_t>(texture->mHeight);
    rgba.resize(texelCount * 4);

    std::string order = trimFormatHint(texture->achFormatHint);
    auto channelIndex = [&](char channel) -> int {
        size_t pos = order.find(channel);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    };

    int rIndex = channelIndex('r');
    int gIndex = channelIndex('g');
    int bIndex = channelIndex('b');
    int aIndex = channelIndex('a');
    const unsigned char* src = reinterpret_cast<const unsigned char*>(texture->pcData);
    for (size_t i = 0; i < texelCount; ++i) {
        const unsigned char* px = src + i * 4;
        rgba[i * 4 + 0] = rIndex >= 0 ? px[rIndex] : 0;
        rgba[i * 4 + 1] = gIndex >= 0 ? px[gIndex] : 0;
        rgba[i * 4 + 2] = bIndex >= 0 ? px[bIndex] : 0;
        rgba[i * 4 + 3] = aIndex >= 0 ? px[aIndex] : 255;
    }
    return true;
}

bool writePNG(const std::filesystem::path& path, const unsigned char* rgba, int width, int height) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return stbi_write_png(path.string().c_str(), width, height, 4, rgba, width * 4) != 0;
}

bool writeFile(const std::filesystem::path& path, const unsigned char* data, size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: extract_embedded_textures <model-path> <output-dir> <asset-root>\n";
        return 1;
    }

    std::string modelPath = argv[1];
    std::filesystem::path outputDir = argv[2];
    std::string assetRoot = argv[3];
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(modelPath, 0);
    if (!scene) {
        std::cerr << "Assimp failed to load model: " << modelPath << "\n";
        return 1;
    }

    int extracted = 0;
    for (unsigned int index = 0; index < scene->mNumTextures; ++index) {
        const aiTexture* texture = scene->mTextures[index];
        if (!texture) {
            continue;
        }

        std::string key = normalizedCacheKey(modelPath, assetRoot, static_cast<int>(index), texture);
        std::string extension = outputExtensionForTexture(texture);
        if (extension.empty()) {
            continue;
        }

        std::filesystem::path outputPath = outputDir / ("embedded_" + hashPathStable(key) + extension);
        bool ok = false;
        if (texture->mHeight == 0) {
            const unsigned char* data = reinterpret_cast<const unsigned char*>(texture->pcData);
            ok = writeFile(outputPath, data, static_cast<size_t>(texture->mWidth));
        } else {
            std::vector<unsigned char> rgba;
            if (!buildEmbeddedRGBA(texture, rgba)) {
                continue;
            }
            ok = writePNG(outputPath, rgba.data(), static_cast<int>(texture->mWidth), static_cast<int>(texture->mHeight));
        }

        if (ok) {
            ++extracted;
        }
    }

    std::cout << "Extracted " << extracted << " embedded texture(s).\n";
    return 0;
}
