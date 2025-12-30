#pragma once

#include <memory>
#include <string>

// Forward declarations
namespace MTL {
    class Device;
    class CommandQueue;
    class ComputePipelineState;
    class Texture;
    class Library;
    class SamplerState;
}

namespace Crescent {

class Texture2D;

/**
 * IBL (Image-Based Lighting) Generator
 * 
 * Generates all necessary textures for physically-based IBL:
 * - BRDF LUT (Lookup table for split-sum approximation)
 * - Cubemap from equirectangular HDR
 * - Prefiltered environment map (specular IBL, roughness in mips)
 * - Irradiance map (diffuse IBL)
 */
class IBLGenerator {
public:
    IBLGenerator();
    ~IBLGenerator();
    
    // Initialize with Metal device
    bool initialize(MTL::Device* device, MTL::CommandQueue* queue);
    void shutdown();
    
    // Generate BRDF LUT (call once at startup)
    // Returns 512x512 RG16Float texture
    MTL::Texture* generateBRDFLUT();
    
    // Convert equirectangular HDR to cubemap
    // Returns cubemap texture (6 faces)
    MTL::Texture* equirectToCubemap(MTL::Texture* equirect, uint32_t resolution = 1024);
    
    // Generate prefiltered environment map for specular IBL
    // Input: cubemap, Output: cubemap with mips for different roughness levels
    MTL::Texture* generatePrefilteredEnvMap(MTL::Texture* cubemap, uint32_t resolution = 512);
    
    // Generate irradiance map for diffuse IBL
    // Input: cubemap, Output: small cubemap (32x32)
    MTL::Texture* generateIrradianceMap(MTL::Texture* cubemap, uint32_t resolution = 32);
    
    // Convenience: Process full IBL from equirectangular HDR
    struct IBLTextures {
        MTL::Texture* cubemap = nullptr;
        MTL::Texture* prefiltered = nullptr;
        MTL::Texture* irradiance = nullptr;
    };
    IBLTextures processEnvironmentMap(MTL::Texture* equirect);
    
    // Get BRDF LUT (generated at init)
    MTL::Texture* getBRDFLUT() const { return m_brdfLUT; }
    
    bool isInitialized() const { return m_initialized; }
    
private:
    bool loadComputeShaders();
    
    MTL::Device* m_device = nullptr;
    MTL::CommandQueue* m_commandQueue = nullptr;
    MTL::Library* m_library = nullptr;
    
    // Compute pipeline states
    MTL::ComputePipelineState* m_brdfLUTPipeline = nullptr;
    MTL::ComputePipelineState* m_equirectToCubePipeline = nullptr;
    MTL::ComputePipelineState* m_prefilteredPipeline = nullptr;
    MTL::ComputePipelineState* m_irradiancePipeline = nullptr;
    
    // Samplers
    MTL::SamplerState* m_linearSampler = nullptr;
    
    // Cached BRDF LUT
    MTL::Texture* m_brdfLUT = nullptr;
    
    bool m_initialized = false;
};

} // namespace Crescent
