#pragma once

#include "../Math/Math.hpp"
#include <vector>
#include <string>
#include <memory>
#include <utility>
#include <array>

namespace Crescent {

// Vertex structure for 3D meshes
struct Vertex {
    Math::Vector3 position;
    Math::Vector3 normal;
    Math::Vector2 texCoord;
    Math::Vector3 tangent;
    Math::Vector3 bitangent;
    Math::Vector4 color;
    
    Vertex()
        : position(Math::Vector3::Zero)
        , normal(Math::Vector3::Up)
        , texCoord(Math::Vector2::Zero)
        , tangent(Math::Vector3::Right)
        , bitangent(Math::Vector3::Forward)
        , color(Math::Vector4::One) {}
};

// Bone influences per vertex (up to 4)
struct SkinWeight {
    std::array<uint32_t, 4> indices;
    std::array<float, 4> weights;

    SkinWeight()
        : indices{0, 0, 0, 0}
        , weights{0.0f, 0.0f, 0.0f, 0.0f} {}
};

// Submesh - a part of a mesh with its own material
struct Submesh {
    uint32_t indexStart;
    uint32_t indexCount;
    uint32_t materialIndex;
    
    Submesh()
        : indexStart(0)
        , indexCount(0)
        , materialIndex(0) {}
    
    Submesh(uint32_t start, uint32_t count, uint32_t matIndex)
        : indexStart(start)
        , indexCount(count)
        , materialIndex(matIndex) {}
};

// Mesh - contains geometry data
class Mesh {
public:
    Mesh();
    ~Mesh();
    
    // Vertex and index data
    void setVertices(const std::vector<Vertex>& vertices);
    void setIndices(const std::vector<uint32_t>& indices);
    void setSubmeshes(const std::vector<Submesh>& submeshes);
    
    const std::vector<Vertex>& getVertices() const { return m_Vertices; }
    const std::vector<uint32_t>& getIndices() const { return m_Indices; }
    const std::vector<Submesh>& getSubmeshes() const { return m_Submeshes; }
    const std::vector<std::pair<uint32_t, uint32_t>>& getWireframeEdges();
    const std::vector<SkinWeight>& getSkinWeights() const { return m_SkinWeights; }
    void setSkinWeights(const std::vector<SkinWeight>& weights);
    bool hasSkinWeights() const { return m_HasSkinWeights; }
    
    // Mesh properties
    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }
    
    // Bounds
    const Math::Vector3& getBoundsMin() const { return m_BoundsMin; }
    const Math::Vector3& getBoundsMax() const { return m_BoundsMax; }
    Math::Vector3 getBoundsCenter() const { return (m_BoundsMin + m_BoundsMax) * 0.5f; }
    Math::Vector3 getBoundsSize() const { return m_BoundsMax - m_BoundsMin; }
    
    void calculateBounds();
    void calculateNormals();
    void calculateTangents();
    
    // GPU resources (void* to avoid Metal types in header)
    void* getVertexBuffer() const { return m_VertexBuffer; }
    void* getIndexBuffer() const { return m_IndexBuffer; }
    void* getSkinWeightBuffer() const { return m_SkinWeightBuffer; }
    
    void setVertexBuffer(void* buffer) { m_VertexBuffer = buffer; }
    void setIndexBuffer(void* buffer) { m_IndexBuffer = buffer; }
    void setSkinWeightBuffer(void* buffer) { m_SkinWeightBuffer = buffer; }
    
    bool isUploaded() const { return m_IsUploaded; }
    void setUploaded(bool uploaded) { m_IsUploaded = uploaded; }
    
    // Double-sided rendering (no culling)
    bool isDoubleSided() const { return m_IsDoubleSided; }
    void setDoubleSided(bool doubleSided) { m_IsDoubleSided = doubleSided; }
    
    // Primitive mesh generation
    static std::shared_ptr<Mesh> CreateCube(float size = 1.0f);
    static std::shared_ptr<Mesh> CreateSphere(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);
    static std::shared_ptr<Mesh> CreatePlane(float width = 1.0f, float height = 1.0f, uint32_t widthSegments = 1, uint32_t heightSegments = 1);
    static std::shared_ptr<Mesh> CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
    static std::shared_ptr<Mesh> CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
    static std::shared_ptr<Mesh> CreateTorus(float majorRadius = 0.75f, float minorRadius = 0.25f, uint32_t majorSegments = 32, uint32_t minorSegments = 16);
    static std::shared_ptr<Mesh> CreateCapsule(float radius = 0.5f, float height = 1.0f, uint32_t segments = 16);
    
private:
    std::string m_Name;
    
    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<Submesh> m_Submeshes;
    std::vector<std::pair<uint32_t, uint32_t>> m_WireEdges;
    std::vector<SkinWeight> m_SkinWeights;
    bool m_WireEdgesDirty;
    
    // Bounds
    Math::Vector3 m_BoundsMin;
    Math::Vector3 m_BoundsMax;
    
    // GPU resources
    void* m_VertexBuffer;
    void* m_IndexBuffer;
    void* m_SkinWeightBuffer;
    bool m_IsUploaded;
    bool m_IsDoubleSided;
    bool m_HasSkinWeights;
};

} // namespace Crescent
