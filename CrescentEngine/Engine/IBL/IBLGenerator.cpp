#include "IBLGenerator.hpp"
#include <iostream>
#include <cmath>
#include <Metal/Metal.hpp>

namespace Crescent {

// Params struct for prefiltered kernel
struct PrefilteredParams {
    float roughness;
    uint32_t resolution;
    uint32_t sampleCount;
    uint32_t face;
};

IBLGenerator::IBLGenerator() = default;

IBLGenerator::~IBLGenerator() {
    shutdown();
}

bool IBLGenerator::initialize(MTL::Device* device, MTL::CommandQueue* queue) {
    if (m_initialized) return true;
    
    m_device = device;
    m_commandQueue = queue;
    
    if (!m_device || !m_commandQueue) {
        std::cerr << "IBLGenerator: Invalid device or command queue!" << std::endl;
        return false;
    }
    
    std::cout << "Initializing IBL Generator..." << std::endl;
    
    // Load compute shaders
    if (!loadComputeShaders()) {
        std::cerr << "IBLGenerator: Failed to load compute shaders!" << std::endl;
        return false;
    }
    
    // Create linear sampler
    MTL::SamplerDescriptor* samplerDesc = MTL::SamplerDescriptor::alloc()->init();
    samplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    samplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    samplerDesc->setMipFilter(MTL::SamplerMipFilterLinear);
    samplerDesc->setSAddressMode(MTL::SamplerAddressModeRepeat);
    samplerDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    samplerDesc->setRAddressMode(MTL::SamplerAddressModeClampToEdge);
    m_linearSampler = m_device->newSamplerState(samplerDesc);
    samplerDesc->release();
    
    // Generate BRDF LUT at startup
    std::cout << "Generating BRDF LUT..." << std::endl;
    m_brdfLUT = generateBRDFLUT();
    if (!m_brdfLUT) {
        std::cerr << "IBLGenerator: Failed to generate BRDF LUT!" << std::endl;
        return false;
    }
    
    m_initialized = true;
    std::cout << "IBL Generator initialized successfully!" << std::endl;
    return true;
}

void IBLGenerator::shutdown() {
    if (m_brdfLUT) {
        m_brdfLUT->release();
        m_brdfLUT = nullptr;
    }
    
    if (m_brdfLUTPipeline) {
        m_brdfLUTPipeline->release();
        m_brdfLUTPipeline = nullptr;
    }
    
    if (m_equirectToCubePipeline) {
        m_equirectToCubePipeline->release();
        m_equirectToCubePipeline = nullptr;
    }
    
    if (m_prefilteredPipeline) {
        m_prefilteredPipeline->release();
        m_prefilteredPipeline = nullptr;
    }
    
    if (m_irradiancePipeline) {
        m_irradiancePipeline->release();
        m_irradiancePipeline = nullptr;
    }
    
    if (m_linearSampler) {
        m_linearSampler->release();
        m_linearSampler = nullptr;
    }
    
    if (m_library) {
        m_library->release();
        m_library = nullptr;
    }
    
    m_initialized = false;
}

bool IBLGenerator::loadComputeShaders() {
    NS::Error* error = nullptr;
    
    // Get default library (contains all .metal files)
    m_library = m_device->newDefaultLibrary();
    if (!m_library) {
        std::cerr << "IBLGenerator: Failed to load shader library!" << std::endl;
        return false;
    }
    
    // BRDF LUT kernel
    MTL::Function* brdfFunc = m_library->newFunction(
        NS::String::string("brdfLUTKernel", NS::UTF8StringEncoding)
    );
    if (brdfFunc) {
        m_brdfLUTPipeline = m_device->newComputePipelineState(brdfFunc, &error);
        brdfFunc->release();
        if (error) {
            std::cerr << "IBLGenerator: BRDF pipeline error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    } else {
        std::cerr << "IBLGenerator: brdfLUTKernel not found!" << std::endl;
    }
    
    // Equirect to Cube kernel
    MTL::Function* equirectFunc = m_library->newFunction(
        NS::String::string("equirectToCubeKernel", NS::UTF8StringEncoding)
    );
    if (equirectFunc) {
        m_equirectToCubePipeline = m_device->newComputePipelineState(equirectFunc, &error);
        equirectFunc->release();
        if (error) {
            std::cerr << "IBLGenerator: Equirect pipeline error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    } else {
        std::cerr << "IBLGenerator: equirectToCubeKernel not found!" << std::endl;
    }
    
    // Prefiltered kernel
    MTL::Function* prefilteredFunc = m_library->newFunction(
        NS::String::string("prefilteredEnvKernel", NS::UTF8StringEncoding)
    );
    if (prefilteredFunc) {
        m_prefilteredPipeline = m_device->newComputePipelineState(prefilteredFunc, &error);
        prefilteredFunc->release();
        if (error) {
            std::cerr << "IBLGenerator: Prefiltered pipeline error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    } else {
        std::cerr << "IBLGenerator: prefilteredEnvKernel not found!" << std::endl;
    }
    
    // Irradiance kernel
    MTL::Function* irradianceFunc = m_library->newFunction(
        NS::String::string("irradianceKernel", NS::UTF8StringEncoding)
    );
    if (irradianceFunc) {
        m_irradiancePipeline = m_device->newComputePipelineState(irradianceFunc, &error);
        irradianceFunc->release();
        if (error) {
            std::cerr << "IBLGenerator: Irradiance pipeline error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    } else {
        std::cerr << "IBLGenerator: irradianceKernel not found!" << std::endl;
    }
    
    return m_brdfLUTPipeline && m_equirectToCubePipeline && m_prefilteredPipeline && m_irradiancePipeline;
}

MTL::Texture* IBLGenerator::generateBRDFLUT() {
    if (!m_brdfLUTPipeline) return nullptr;
    
    const uint32_t size = 512;
    
    // Create output texture
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setPixelFormat(MTL::PixelFormatRG16Float);
    desc->setWidth(size);
    desc->setHeight(size);
    desc->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModePrivate);
    
    MTL::Texture* brdfLUT = m_device->newTexture(desc);
    desc->release();
    
    if (!brdfLUT) return nullptr;
    
    // Dispatch compute
    MTL::CommandBuffer* cmdBuffer = m_commandQueue->commandBuffer();
    MTL::ComputeCommandEncoder* encoder = cmdBuffer->computeCommandEncoder();
    
    encoder->setComputePipelineState(m_brdfLUTPipeline);
    encoder->setTexture(brdfLUT, 0);
    
    MTL::Size threadGroupSize = MTL::Size::Make(16, 16, 1);
    MTL::Size gridSize = MTL::Size::Make(size, size, 1);
    
    encoder->dispatchThreads(gridSize, threadGroupSize);
    encoder->endEncoding();
    
    cmdBuffer->commit();
    cmdBuffer->waitUntilCompleted();
    
    std::cout << "BRDF LUT generated (" << size << "x" << size << ")" << std::endl;
    return brdfLUT;
}

MTL::Texture* IBLGenerator::equirectToCubemap(MTL::Texture* equirect, uint32_t resolution) {
    if (!m_equirectToCubePipeline || !equirect) return nullptr;

    const uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(resolution))) + 1;
    
    // Create cubemap texture
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureTypeCube);
    desc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setWidth(resolution);
    desc->setHeight(resolution);
    desc->setMipmapLevelCount(mipLevels);
    desc->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModePrivate);
    
    MTL::Texture* cubemap = m_device->newTexture(desc);
    desc->release();
    
    if (!cubemap) return nullptr;
    
    // Dispatch compute for all 6 faces
    MTL::CommandBuffer* cmdBuffer = m_commandQueue->commandBuffer();
    MTL::ComputeCommandEncoder* encoder = cmdBuffer->computeCommandEncoder();
    
    encoder->setComputePipelineState(m_equirectToCubePipeline);
    encoder->setTexture(equirect, 0);
    encoder->setTexture(cubemap, 1);
    encoder->setSamplerState(m_linearSampler, 0);
    
    MTL::Size threadGroupSize = MTL::Size::Make(16, 16, 1);
    MTL::Size gridSize = MTL::Size::Make(resolution, resolution, 6);
    
    encoder->dispatchThreads(gridSize, threadGroupSize);
    encoder->endEncoding();
    
    cmdBuffer->commit();
    cmdBuffer->waitUntilCompleted();

    // Build mip chain so specular prefilter can sample proper LODs
    MTL::CommandBuffer* blitCmd = m_commandQueue->commandBuffer();
    if (blitCmd) {
        MTL::BlitCommandEncoder* blit = blitCmd->blitCommandEncoder();
        if (blit) {
            blit->generateMipmaps(cubemap);
            blit->endEncoding();
        }
        blitCmd->commit();
        blitCmd->waitUntilCompleted();
    }
    
    std::cout << "Cubemap generated (" << resolution << "x" << resolution << " x 6 faces)" << std::endl;
    return cubemap;
}

MTL::Texture* IBLGenerator::generatePrefilteredEnvMap(MTL::Texture* cubemap, uint32_t resolution) {
    if (!m_prefilteredPipeline || !cubemap) return nullptr;
    
    const uint32_t maxMipLevels = 6; // 6 roughness levels for better specular quality
    
    // Create prefiltered cubemap with mipmaps
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureTypeCube);
    desc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setWidth(resolution);
    desc->setHeight(resolution);
    desc->setMipmapLevelCount(maxMipLevels);
    desc->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModePrivate);
    
    MTL::Texture* prefiltered = m_device->newTexture(desc);
    desc->release();
    
    if (!prefiltered) return nullptr;
    
    // Process each mip level
    for (uint32_t mip = 0; mip < maxMipLevels; ++mip) {
        uint32_t mipSize = resolution >> mip;

        NS::Range mipRange   = NS::Range::Make(mip, 1);
        NS::Range sliceRange = NS::Range::Make(0, 6); // ✅ mutlaka 6

        MTL::Texture* mipCubeView = prefiltered->newTextureView(
            MTL::PixelFormatRGBA16Float,
            MTL::TextureTypeCube,
            mipRange,
            sliceRange
        );

        for (uint32_t face = 0; face < 6; ++face) {
            PrefilteredParams params;
            params.roughness   = float(mip) / float(maxMipLevels - 1);
            params.resolution  = mipSize;
            // More samples for lower roughness to reduce sun aliasing
            // Mip 0 (roughness=0) needs many samples for sharp reflections without aliasing
            params.sampleCount = (mip == 0) ? 2048 : (mip == 1) ? 1024 : 512;
            params.face        = face;

            auto* cmdBuffer = m_commandQueue->commandBuffer();
            auto* encoder   = cmdBuffer->computeCommandEncoder();

            encoder->setComputePipelineState(m_prefilteredPipeline);
            encoder->setTexture(cubemap, 0);
            encoder->setTexture(mipCubeView, 1);          
            encoder->setSamplerState(m_linearSampler, 0);
            encoder->setBytes(&params, sizeof(params), 0);

            MTL::Size tg   = MTL::Size::Make(16, 16, 1);
            MTL::Size grid = MTL::Size::Make(mipSize, mipSize, 1);
            encoder->dispatchThreads(grid, tg);
            encoder->endEncoding();

            cmdBuffer->commit();
            cmdBuffer->waitUntilCompleted();
        }

        mipCubeView->release();
    }

    
    std::cout << "Prefiltered environment map generated (" << resolution << "x" << resolution << " x " << maxMipLevels << " mips)" << std::endl;
    return prefiltered;
}

MTL::Texture* IBLGenerator::generateIrradianceMap(MTL::Texture* cubemap, uint32_t resolution) {
    if (!m_irradiancePipeline || !cubemap) return nullptr;
    
    // Create irradiance cubemap
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureTypeCube);
    desc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setWidth(resolution);
    desc->setHeight(resolution);
    desc->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModePrivate);
    
    MTL::Texture* irradiance = m_device->newTexture(desc);
    desc->release();
    
    if (!irradiance) return nullptr;
    
    // Process each face
    for (uint32_t face = 0; face < 6; ++face) {
        MTL::CommandBuffer* cmdBuffer = m_commandQueue->commandBuffer();
        MTL::ComputeCommandEncoder* encoder = cmdBuffer->computeCommandEncoder();
        
        encoder->setComputePipelineState(m_irradiancePipeline);
        encoder->setTexture(cubemap, 0);
        encoder->setTexture(irradiance, 1);
        encoder->setSamplerState(m_linearSampler, 0);
        encoder->setBytes(&face, sizeof(face), 0);
        
        MTL::Size threadGroupSize = MTL::Size::Make(8, 8, 1);
        MTL::Size gridSize = MTL::Size::Make(resolution, resolution, 1);
        
        encoder->dispatchThreads(gridSize, threadGroupSize);
        encoder->endEncoding();
        
        cmdBuffer->commit();
        cmdBuffer->waitUntilCompleted();
    }
    
    std::cout << "Irradiance map generated (" << resolution << "x" << resolution << " x 6 faces)" << std::endl;
    return irradiance;
}

IBLGenerator::IBLTextures IBLGenerator::processEnvironmentMap(MTL::Texture* equirect) {
    IBLTextures result;
    
    if (!equirect || !m_initialized) {
        std::cerr << "IBLGenerator: Cannot process - not initialized or no input!" << std::endl;
        return result;
    }
    
    std::cout << "Processing environment map for IBL..." << std::endl;
    
    // Step 1: Convert equirectangular to cubemap
    result.cubemap = equirectToCubemap(equirect, 1024);
    if (!result.cubemap) {
        std::cerr << "IBLGenerator: Failed to create cubemap!" << std::endl;
        return result;
    }
    
    // Step 2: Generate prefiltered environment map (specular)
    result.prefiltered = generatePrefilteredEnvMap(result.cubemap, 512);
    if (!result.prefiltered) {
        std::cerr << "IBLGenerator: Failed to create prefiltered map!" << std::endl;
    }
    
    // Step 3: Generate irradiance map (diffuse)
    result.irradiance = generateIrradianceMap(result.cubemap, 32);
    if (!result.irradiance) {
        std::cerr << "IBLGenerator: Failed to create irradiance map!" << std::endl;
    }
    
    std::cout << "Environment map IBL processing complete!" << std::endl;
    return result;
}

} // namespace Crescent
