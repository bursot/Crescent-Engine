#include "DebugRenderer.hpp"
#include <Metal/Metal.hpp>
#include <iostream>
#include <cmath>

namespace Crescent {

// Uniform data for debug rendering (must match Debug.metal)
struct alignas(16) DebugUniforms {
    Math::Matrix4x4 viewProjectionMatrix; // 64 bytes
    Math::Vector4 gridColor;              // 16 bytes (offset 64)
    Math::Vector4 cameraPosition;         // 16 bytes (offset 80) - w unused
    Math::Vector4 gridParams;             // 16 bytes (offset 96)  - (gridSize, gridFadeStart, gridFadeEnd, gridCellSize)
    Math::Vector2 gridOrigin;             // 8 bytes  (offset 112) - snapped origin in XZ
    Math::Vector2 padding;                // 8 bytes  (offset 120) - keep 128-byte size
};

static_assert(sizeof(DebugUniforms) == 128, "DebugUniforms size must match Metal side");

DebugRenderer::DebugRenderer()
    : m_lineBuffer(nullptr)
    , m_linePipelineState(nullptr)
    , m_lineBufferCapacity(10000)
    , m_gridEnabled(true)
    , m_gridSize(400.0f)           // World grid span
    , m_gridCellSize(1.0f)         // 1 meter cells
    , m_gridColor(0.65f, 0.72f, 0.82f, 0.8f)  // Softer blue-gray
    , m_gridFadeStart(35.0f)
    , m_gridFadeEnd(80.0f)
    , m_gridOriginX(0.0f)
    , m_gridOriginZ(0.0f)
    , m_gridVertexBuffer(nullptr)
    , m_gridPipelineState(nullptr)
    , m_gridVertexCount(0)
    , m_debugUniformBuffer(nullptr)
    , m_device(nullptr)
    , m_initialized(false) {
}

DebugRenderer::~DebugRenderer() {
    shutdown();
}

bool DebugRenderer::initialize(MTL::Device* device) {
    if (m_initialized) {
        std::cout << "DebugRenderer already initialized!" << std::endl;
        return true;
    }
    
    if (!device) {
        std::cerr << "Invalid Metal device for DebugRenderer!" << std::endl;
        return false;
    }
    
    m_device = device;
    
    std::cout << "Initializing DebugRenderer..." << std::endl;
    
    // Create line buffer (dynamic)
    m_lineBuffer = device->newBuffer(
        m_lineBufferCapacity * sizeof(DebugVertex),
        MTL::ResourceStorageModeShared
    );
    
    if (!m_lineBuffer) {
        std::cerr << "Failed to create debug line buffer!" << std::endl;
        return false;
    }
    
    // Create initial grid geometry at origin
    updateGridGeometry(Math::Vector3::Zero);
    
    // Create uniform buffer
    m_debugUniformBuffer = device->newBuffer(
        sizeof(DebugUniforms),
        MTL::ResourceStorageModeShared
    );
    
    m_initialized = true;
    std::cout << "DebugRenderer initialized successfully" << std::endl;
    std::cout << "  Grid enabled: " << (m_gridEnabled ? "Yes" : "No") << std::endl;
    std::cout << "  Grid size: " << m_gridSize << " units" << std::endl;
    std::cout << "  Grid cell size: " << m_gridCellSize << " unit" << std::endl;
    
    return true;
}

void DebugRenderer::shutdown() {
    if (!m_initialized) return;
    
    if (m_lineBuffer) {
        m_lineBuffer->release();
        m_lineBuffer = nullptr;
    }
    
    if (m_linePipelineState) {
        m_linePipelineState->release();
        m_linePipelineState = nullptr;
    }
    
    if (m_gridVertexBuffer) {
        m_gridVertexBuffer->release();
        m_gridVertexBuffer = nullptr;
    }
    
    if (m_gridPipelineState) {
        m_gridPipelineState->release();
        m_gridPipelineState = nullptr;
    }
    
    if (m_debugUniformBuffer) {
        m_debugUniformBuffer->release();
        m_debugUniformBuffer = nullptr;
    }
    
    m_initialized = false;
    std::cout << "DebugRenderer shutdown complete" << std::endl;
}

void DebugRenderer::createPipelineStates(MTL::Device* device) {
    // Pipeline states created in Renderer class
}

void DebugRenderer::createGridGeometry() {
    // Legacy entry point; build once at origin
    updateGridGeometry(Math::Vector3::Zero);
}

void DebugRenderer::updateGridGeometry(const Math::Vector3& cameraPosition) {
    if (!m_device) return;
    
    // World-locked quad (XZ plane) centered around snapped origin
    float halfSize = m_gridSize * 0.5f;
    float targetOriginX = std::floor(cameraPosition.x / m_gridCellSize) * m_gridCellSize;
    float targetOriginZ = std::floor(cameraPosition.z / m_gridCellSize) * m_gridCellSize;
    m_gridOriginX = targetOriginX;
    m_gridOriginZ = targetOriginZ;

    float originX = m_gridOriginX;
    float originZ = m_gridOriginZ;
    float y = 0.01f; // just above ground
    
    Math::Vector3 bl(originX - halfSize, y, originZ - halfSize);
    Math::Vector3 br(originX + halfSize, y, originZ - halfSize);
    Math::Vector3 tl(originX - halfSize, y, originZ + halfSize);
    Math::Vector3 tr(originX + halfSize, y, originZ + halfSize);
    
    Math::Vector4 quadColor(1.0f, 1.0f, 1.0f, 1.0f); // actual tint handled in shader via uniforms
    
    std::vector<DebugVertex> verts = {
        // Triangle 1
        DebugVertex(bl, quadColor),
        DebugVertex(br, quadColor),
        DebugVertex(tl, quadColor),
        // Triangle 2
        DebugVertex(tl, quadColor),
        DebugVertex(br, quadColor),
        DebugVertex(tr, quadColor)
    };
    
    m_gridVertexCount = verts.size();
    
    size_t requiredSize = m_gridVertexCount * sizeof(DebugVertex);
    if (!m_gridVertexBuffer || requiredSize > m_gridVertexBuffer->length()) {
        if (m_gridVertexBuffer) {
            m_gridVertexBuffer->release();
        }
        m_gridVertexBuffer = m_device->newBuffer(
            requiredSize,
            MTL::ResourceStorageModeShared
        );
        std::cout << "Grid buffer resized to " << m_gridVertexCount << " vertices" << std::endl;
    }
    
    memcpy(m_gridVertexBuffer->contents(), verts.data(), requiredSize);
}

void DebugRenderer::clear() {
    m_lines.clear();
}

void DebugRenderer::render(MTL::RenderCommandEncoder* encoder,
                           const Math::Matrix4x4& viewMatrix,
                           const Math::Matrix4x4& projectionMatrix,
                           const Math::Vector3& cameraPosition) {
    if (!m_initialized) return;
    
    // Upload line data to GPU buffer
    uploadLineData();
    
    // Rebuild grid around the camera each frame
    updateGridGeometry(cameraPosition);
    
    // Upload uniform data
    DebugUniforms uniforms{};
    uniforms.viewProjectionMatrix = projectionMatrix * viewMatrix;
    uniforms.gridColor = m_gridColor;
    uniforms.cameraPosition = Math::Vector4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f);
    // CRITICAL: gridParams = (gridSize, fadeStart, fadeEnd, cellSize)
    // Shader reads cellSize from .w (last component)!
    uniforms.gridParams = Math::Vector4(m_gridSize, m_gridFadeStart, m_gridFadeEnd, m_gridCellSize);
    uniforms.gridOrigin = Math::Vector2(m_gridOriginX, m_gridOriginZ);
    uniforms.padding = Math::Vector2(0.0f, 0.0f);
    
    memcpy(m_debugUniformBuffer->contents(), &uniforms, sizeof(DebugUniforms));
    
    // Render will be handled in Renderer class
}

void DebugRenderer::renderLines(MTL::RenderCommandEncoder* encoder,
                                const Math::Matrix4x4& viewMatrix,
                                const Math::Matrix4x4& projectionMatrix) {
    uploadLineData();
}

void DebugRenderer::renderGrid(MTL::RenderCommandEncoder* encoder,
                               const Math::Matrix4x4& viewMatrix,
                               const Math::Matrix4x4& projectionMatrix) {
}

void DebugRenderer::uploadLineData() {
    if (m_lines.empty()) return;
    
    size_t requiredSize = m_lines.size() * sizeof(DebugVertex);
    if (requiredSize > m_lineBuffer->length()) {
        m_lineBuffer->release();
        m_lineBufferCapacity = m_lines.size() * 2;
        m_lineBuffer = m_device->newBuffer(
            m_lineBufferCapacity * sizeof(DebugVertex),
            MTL::ResourceStorageModeShared
        );
        std::cout << "Debug line buffer resized to " << m_lineBufferCapacity << " vertices" << std::endl;
    }
    
    memcpy(m_lineBuffer->contents(), m_lines.data(), requiredSize);
}

void DebugRenderer::drawLine(const Math::Vector3& start, const Math::Vector3& end,
                             const Math::Vector4& color) {
    m_lines.push_back(DebugVertex(start, color));
    m_lines.push_back(DebugVertex(end, color));
}

void DebugRenderer::drawBox(const Math::Vector3& center, const Math::Vector3& size,
                            const Math::Vector4& color) {
    Math::Vector3 halfSize = size * 0.5f;
    
    Math::Vector3 v0 = center + Math::Vector3(-halfSize.x, -halfSize.y, -halfSize.z);
    Math::Vector3 v1 = center + Math::Vector3( halfSize.x, -halfSize.y, -halfSize.z);
    Math::Vector3 v2 = center + Math::Vector3( halfSize.x, -halfSize.y,  halfSize.z);
    Math::Vector3 v3 = center + Math::Vector3(-halfSize.x, -halfSize.y,  halfSize.z);
    
    Math::Vector3 v4 = center + Math::Vector3(-halfSize.x, halfSize.y, -halfSize.z);
    Math::Vector3 v5 = center + Math::Vector3( halfSize.x, halfSize.y, -halfSize.z);
    Math::Vector3 v6 = center + Math::Vector3( halfSize.x, halfSize.y,  halfSize.z);
    Math::Vector3 v7 = center + Math::Vector3(-halfSize.x, halfSize.y,  halfSize.z);
    
    // Bottom face
    drawLine(v0, v1, color);
    drawLine(v1, v2, color);
    drawLine(v2, v3, color);
    drawLine(v3, v0, color);
    
    // Top face
    drawLine(v4, v5, color);
    drawLine(v5, v6, color);
    drawLine(v6, v7, color);
    drawLine(v7, v4, color);
    
    // Vertical edges
    drawLine(v0, v4, color);
    drawLine(v1, v5, color);
    drawLine(v2, v6, color);
    drawLine(v3, v7, color);
}

void DebugRenderer::drawSphere(const Math::Vector3& center, float radius,
                               const Math::Vector4& color, int segments) {
    float angleStep = Math::TWO_PI / segments;
    
    // XY plane
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        Math::Vector3 p1 = center + Math::Vector3(
            std::cos(angle1) * radius,
            std::sin(angle1) * radius,
            0
        );
        Math::Vector3 p2 = center + Math::Vector3(
            std::cos(angle2) * radius,
            std::sin(angle2) * radius,
            0
        );
        
        drawLine(p1, p2, color);
    }
    
    // XZ plane
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        Math::Vector3 p1 = center + Math::Vector3(
            std::cos(angle1) * radius,
            0,
            std::sin(angle1) * radius
        );
        Math::Vector3 p2 = center + Math::Vector3(
            std::cos(angle2) * radius,
            0,
            std::sin(angle2) * radius
        );
        
        drawLine(p1, p2, color);
    }
    
    // YZ plane
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        Math::Vector3 p1 = center + Math::Vector3(
            0,
            std::cos(angle1) * radius,
            std::sin(angle1) * radius
        );
        Math::Vector3 p2 = center + Math::Vector3(
            0,
            std::cos(angle2) * radius,
            std::sin(angle2) * radius
        );
        
        drawLine(p1, p2, color);
    }
}

void DebugRenderer::drawAxes(const Math::Vector3& position, float size) {
    // X axis - Red
    drawLine(position, position + Math::Vector3::Right * size,
             Math::Vector4(1, 0, 0, 1));
    
    // Y axis - Green
    drawLine(position, position + Math::Vector3::Up * size,
             Math::Vector4(0, 1, 0, 1));
    
    // Z axis - Blue
    drawLine(position, position + Math::Vector3::Forward * size,
             Math::Vector4(0, 0, 1, 1));
}

void DebugRenderer::drawRay(const Math::Vector3& origin, const Math::Vector3& direction,
                            float length, const Math::Vector4& color) {
    Math::Vector3 end = origin + direction.normalized() * length;
    drawLine(origin, end, color);
}

} // namespace Crescent
