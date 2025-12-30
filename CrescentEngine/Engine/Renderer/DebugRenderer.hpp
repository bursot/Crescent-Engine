#pragma once

#include "../Math/Math.hpp"
#include <vector>
#include <memory>

// Forward declarations
namespace MTL {
    class Device;
    class RenderCommandEncoder;
    class RenderPipelineState;
    class Buffer;
}

namespace Crescent {

// Debug line vertex
struct DebugVertex {
    Math::Vector3 position;
    Math::Vector4 color;
    
    DebugVertex() = default;
    DebugVertex(const Math::Vector3& pos, const Math::Vector4& col)
        : position(pos), color(col) {}
};

// Debug rendering system for lines, grids, bounding boxes, axes
class DebugRenderer {
public:
    DebugRenderer();
    ~DebugRenderer();
    
    bool initialize(MTL::Device* device);
    void shutdown();
    
    // Render all debug geometry
    void render(MTL::RenderCommandEncoder* encoder, 
                const Math::Matrix4x4& viewMatrix,
                const Math::Matrix4x4& projectionMatrix,
                const Math::Vector3& cameraPosition);
    
    // Clear all debug geometry (called each frame)
    void clear();
    
    // === LINE DRAWING ===
    
    // Draw a line between two points
    void drawLine(const Math::Vector3& start, const Math::Vector3& end, 
                  const Math::Vector4& color = Math::Vector4(1, 1, 1, 1));
    
    // Draw a wireframe box
    void drawBox(const Math::Vector3& center, const Math::Vector3& size,
                 const Math::Vector4& color = Math::Vector4(1, 1, 1, 1));
    
    // Draw a wireframe sphere (approximation with lines)
    void drawSphere(const Math::Vector3& center, float radius,
                    const Math::Vector4& color = Math::Vector4(1, 1, 1, 1),
                    int segments = 16);
    
    // Draw transform axes (X=red, Y=green, Z=blue)
    void drawAxes(const Math::Vector3& position, float size = 1.0f);
    
    // Draw a ray
    void drawRay(const Math::Vector3& origin, const Math::Vector3& direction,
                 float length = 10.0f,
                 const Math::Vector4& color = Math::Vector4(1, 1, 0, 1));
    
    // === GRID ===
    
    // Enable/disable infinite grid
    void setGridEnabled(bool enabled) { m_gridEnabled = enabled; }
    bool isGridEnabled() const { return m_gridEnabled; }
    
    // Set grid properties
    void setGridSize(float size) { m_gridSize = size; }
    void setGridCellSize(float cellSize) { m_gridCellSize = cellSize; }
    void setGridColor(const Math::Vector4& color) { m_gridColor = color; }
    void setGridFadeStart(float distance) { m_gridFadeStart = distance; }
    void setGridFadeEnd(float distance) { m_gridFadeEnd = distance; }
    
    // Get buffers for rendering
    MTL::Buffer* getLineBuffer() { return m_lineBuffer; }
    MTL::Buffer* getGridBuffer() { return m_gridVertexBuffer; }
    MTL::Buffer* getUniformBuffer() { return m_debugUniformBuffer; }
    size_t getLineCount() const { return m_lines.size(); }
    size_t getGridVertexCount() const { return m_gridVertexCount; }
    
private:
    void createPipelineStates(MTL::Device* device);
    void renderLines(MTL::RenderCommandEncoder* encoder,
                     const Math::Matrix4x4& viewMatrix,
                     const Math::Matrix4x4& projectionMatrix);
    void renderGrid(MTL::RenderCommandEncoder* encoder,
                    const Math::Matrix4x4& viewMatrix,
                    const Math::Matrix4x4& projectionMatrix);
    
    void uploadLineData();
    void createGridGeometry();
    void updateGridGeometry(const Math::Vector3& cameraPosition);
    
private:
    // Line rendering
    std::vector<DebugVertex> m_lines;
    MTL::Buffer* m_lineBuffer;
    MTL::RenderPipelineState* m_linePipelineState;
    size_t m_lineBufferCapacity;
    
    // Grid rendering
    bool m_gridEnabled;
    float m_gridSize;
    float m_gridCellSize;
    Math::Vector4 m_gridColor;
    float m_gridFadeStart;
    float m_gridFadeEnd;
    float m_gridOriginX;
    float m_gridOriginZ;
    
    MTL::Buffer* m_gridVertexBuffer;
    MTL::RenderPipelineState* m_gridPipelineState;
    size_t m_gridVertexCount;
    
    // Uniform buffers
    MTL::Buffer* m_debugUniformBuffer;
    
    MTL::Device* m_device;
    bool m_initialized;
};

} // namespace Crescent
