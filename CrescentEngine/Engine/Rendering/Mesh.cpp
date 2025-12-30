#include "Mesh.hpp"
#include <limits>
#include <cmath>
#include <unordered_set>
#include <algorithm>

namespace Crescent {

Mesh::Mesh()
    : m_Name("Mesh")
    , m_WireEdgesDirty(true)
    , m_BoundsMin(Math::Vector3(std::numeric_limits<float>::max()))
    , m_BoundsMax(Math::Vector3(std::numeric_limits<float>::lowest()))
    , m_VertexBuffer(nullptr)
    , m_IndexBuffer(nullptr)
    , m_SkinWeightBuffer(nullptr)
    , m_IsUploaded(false)
    , m_IsDoubleSided(false)
    , m_HasSkinWeights(false) {
}

Mesh::~Mesh() {
    // GPU resources are managed by Renderer
}

void Mesh::setVertices(const std::vector<Vertex>& vertices) {
    m_Vertices = vertices;
    calculateBounds();
    m_IsUploaded = false;
    m_WireEdgesDirty = true;
}

void Mesh::setIndices(const std::vector<uint32_t>& indices) {
    m_Indices = indices;
    m_IsUploaded = false;
    m_WireEdgesDirty = true;
}

void Mesh::setSubmeshes(const std::vector<Submesh>& submeshes) {
    m_Submeshes = submeshes;
}

void Mesh::setSkinWeights(const std::vector<SkinWeight>& weights) {
    m_SkinWeights = weights;
    m_HasSkinWeights = !m_SkinWeights.empty();
    m_IsUploaded = false;
}

const std::vector<std::pair<uint32_t, uint32_t>>& Mesh::getWireframeEdges() {
    if (!m_WireEdgesDirty) {
        return m_WireEdges;
    }

    m_WireEdges.clear();
    if (m_Indices.empty()) {
        m_WireEdgesDirty = false;
        return m_WireEdges;
    }

    struct EdgeHash {
        size_t operator()(const std::pair<uint32_t, uint32_t>& e) const {
            return (static_cast<size_t>(e.first) << 32) ^ e.second;
        }
    };

    std::unordered_set<std::pair<uint32_t, uint32_t>, EdgeHash> edges;
    edges.reserve(m_Indices.size());

    for (size_t i = 0; i + 2 < m_Indices.size(); i += 3) {
        uint32_t a = m_Indices[i];
        uint32_t b = m_Indices[i + 1];
        uint32_t c = m_Indices[i + 2];

        if (a == b || b == c || c == a) {
            continue;
        }

        edges.insert(std::minmax(a, b));
        edges.insert(std::minmax(b, c));
        edges.insert(std::minmax(c, a));
    }

    m_WireEdges.reserve(edges.size());
    for (const auto& edge : edges) {
        m_WireEdges.push_back(edge);
    }

    m_WireEdgesDirty = false;
    return m_WireEdges;
}

void Mesh::calculateBounds() {
    if (m_Vertices.empty()) {
        m_BoundsMin = Math::Vector3::Zero;
        m_BoundsMax = Math::Vector3::Zero;
        return;
    }
    
    m_BoundsMin = Math::Vector3(std::numeric_limits<float>::max());
    m_BoundsMax = Math::Vector3(std::numeric_limits<float>::lowest());
    
    for (const auto& vertex : m_Vertices) {
        m_BoundsMin = Math::Vector3::Min(m_BoundsMin, vertex.position);
        m_BoundsMax = Math::Vector3::Max(m_BoundsMax, vertex.position);
    }
}

void Mesh::calculateNormals() {
    if (m_Vertices.empty() || m_Indices.empty()) return;
    
    // Reset normals
    for (auto& vertex : m_Vertices) {
        vertex.normal = Math::Vector3::Zero;
    }
    
    // Calculate face normals and accumulate
    for (size_t i = 0; i < m_Indices.size(); i += 3) {
        uint32_t i0 = m_Indices[i];
        uint32_t i1 = m_Indices[i + 1];
        uint32_t i2 = m_Indices[i + 2];
        
        Math::Vector3 v0 = m_Vertices[i0].position;
        Math::Vector3 v1 = m_Vertices[i1].position;
        Math::Vector3 v2 = m_Vertices[i2].position;
        
        Math::Vector3 edge1 = v1 - v0;
        Math::Vector3 edge2 = v2 - v0;
        Math::Vector3 normal = edge1.cross(edge2);
        
        m_Vertices[i0].normal += normal;
        m_Vertices[i1].normal += normal;
        m_Vertices[i2].normal += normal;
    }
    
    // Normalize
    for (auto& vertex : m_Vertices) {
        vertex.normal.normalize();
    }
    
    m_IsUploaded = false;
}

void Mesh::calculateTangents() {
    if (m_Vertices.empty() || m_Indices.empty()) return;
    
    // Reset tangents and bitangents
    for (auto& vertex : m_Vertices) {
        vertex.tangent = Math::Vector3::Zero;
        vertex.bitangent = Math::Vector3::Zero;
    }
    
    // Calculate tangents using texture coordinates
    for (size_t i = 0; i < m_Indices.size(); i += 3) {
        uint32_t i0 = m_Indices[i];
        uint32_t i1 = m_Indices[i + 1];
        uint32_t i2 = m_Indices[i + 2];
        
        const Vertex& v0 = m_Vertices[i0];
        const Vertex& v1 = m_Vertices[i1];
        const Vertex& v2 = m_Vertices[i2];
        
        Math::Vector3 edge1 = v1.position - v0.position;
        Math::Vector3 edge2 = v2.position - v0.position;
        
        Math::Vector2 deltaUV1 = v1.texCoord - v0.texCoord;
        Math::Vector2 deltaUV2 = v2.texCoord - v0.texCoord;
        
        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        
        Math::Vector3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        
        Math::Vector3 bitangent;
        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
        
        m_Vertices[i0].tangent += tangent;
        m_Vertices[i1].tangent += tangent;
        m_Vertices[i2].tangent += tangent;
        
        m_Vertices[i0].bitangent += bitangent;
        m_Vertices[i1].bitangent += bitangent;
        m_Vertices[i2].bitangent += bitangent;
    }
    
    // Normalize and orthogonalize
    for (auto& vertex : m_Vertices) {
        // Gram-Schmidt orthogonalization
        vertex.tangent = (vertex.tangent - vertex.normal * vertex.normal.dot(vertex.tangent)).normalized();
        vertex.bitangent = vertex.bitangent.normalized();
    }
    
    m_IsUploaded = false;
}

// ============================================================================
// PRIMITIVE MESH GENERATION
// ============================================================================

std::shared_ptr<Mesh> Mesh::CreateCube(float size) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setName("Cube");
    
    float halfSize = size * 0.5f;
    
    std::vector<Vertex> vertices;
    vertices.reserve(24);
    
    // Helper to create vertex
    auto makeVertex = [](float px, float py, float pz, 
                         float nx, float ny, float nz,
                         float u, float vtex,
                         float tx, float ty, float tz) -> Vertex {
        Vertex vert;
        vert.position = Math::Vector3(px, py, pz);
        vert.normal = Math::Vector3(nx, ny, nz);
        vert.texCoord = Math::Vector2(u, vtex);
        vert.tangent = Math::Vector3(tx, ty, tz);
        vert.bitangent = Math::Vector3(0, 1, 0);
        vert.color = Math::Vector4(1, 1, 1, 1);
        return vert;
    };
    
    // Front face
    vertices.push_back(makeVertex(-halfSize, -halfSize,  halfSize, 0, 0, 1, 0, 0, 1, 0, 0));
    vertices.push_back(makeVertex( halfSize, -halfSize,  halfSize, 0, 0, 1, 1, 0, 1, 0, 0));
    vertices.push_back(makeVertex( halfSize,  halfSize,  halfSize, 0, 0, 1, 1, 1, 1, 0, 0));
    vertices.push_back(makeVertex(-halfSize,  halfSize,  halfSize, 0, 0, 1, 0, 1, 1, 0, 0));
    
    // Back face
    vertices.push_back(makeVertex( halfSize, -halfSize, -halfSize, 0, 0, -1, 0, 0, -1, 0, 0));
    vertices.push_back(makeVertex(-halfSize, -halfSize, -halfSize, 0, 0, -1, 1, 0, -1, 0, 0));
    vertices.push_back(makeVertex(-halfSize,  halfSize, -halfSize, 0, 0, -1, 1, 1, -1, 0, 0));
    vertices.push_back(makeVertex( halfSize,  halfSize, -halfSize, 0, 0, -1, 0, 1, -1, 0, 0));
    
    // Top face
    vertices.push_back(makeVertex(-halfSize,  halfSize,  halfSize, 0, 1, 0, 0, 0, 1, 0, 0));
    vertices.push_back(makeVertex( halfSize,  halfSize,  halfSize, 0, 1, 0, 1, 0, 1, 0, 0));
    vertices.push_back(makeVertex( halfSize,  halfSize, -halfSize, 0, 1, 0, 1, 1, 1, 0, 0));
    vertices.push_back(makeVertex(-halfSize,  halfSize, -halfSize, 0, 1, 0, 0, 1, 1, 0, 0));
    
    // Bottom face
    vertices.push_back(makeVertex(-halfSize, -halfSize, -halfSize, 0, -1, 0, 0, 0, 1, 0, 0));
    vertices.push_back(makeVertex( halfSize, -halfSize, -halfSize, 0, -1, 0, 1, 0, 1, 0, 0));
    vertices.push_back(makeVertex( halfSize, -halfSize,  halfSize, 0, -1, 0, 1, 1, 1, 0, 0));
    vertices.push_back(makeVertex(-halfSize, -halfSize,  halfSize, 0, -1, 0, 0, 1, 1, 0, 0));
    
    // Right face
    vertices.push_back(makeVertex( halfSize, -halfSize,  halfSize, 1, 0, 0, 0, 0, 0, 0, -1));
    vertices.push_back(makeVertex( halfSize, -halfSize, -halfSize, 1, 0, 0, 1, 0, 0, 0, -1));
    vertices.push_back(makeVertex( halfSize,  halfSize, -halfSize, 1, 0, 0, 1, 1, 0, 0, -1));
    vertices.push_back(makeVertex( halfSize,  halfSize,  halfSize, 1, 0, 0, 0, 1, 0, 0, -1));
    
    // Left face
    vertices.push_back(makeVertex(-halfSize, -halfSize, -halfSize, -1, 0, 0, 0, 0, 0, 0, 1));
    vertices.push_back(makeVertex(-halfSize, -halfSize,  halfSize, -1, 0, 0, 1, 0, 0, 0, 1));
    vertices.push_back(makeVertex(-halfSize,  halfSize,  halfSize, -1, 0, 0, 1, 1, 0, 0, 1));
    vertices.push_back(makeVertex(-halfSize,  halfSize, -halfSize, -1, 0, 0, 0, 1, 0, 0, 1));
    
    std::vector<uint32_t> indices = {
        0,  1,  2,  0,  2,  3,   // Front
        4,  5,  6,  4,  6,  7,   // Back
        8,  9,  10, 8,  10, 11,  // Top
        12, 13, 14, 12, 14, 15,  // Bottom
        16, 17, 18, 16, 18, 19,  // Right
        20, 21, 22, 20, 22, 23   // Left
    };
    
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreateSphere(float radius, uint32_t segments, uint32_t rings) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setName("Sphere");
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Generate vertices
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = Math::PI * static_cast<float>(ring) / static_cast<float>(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);
        
        for (uint32_t segment = 0; segment <= segments; ++segment) {
            float theta = 2.0f * Math::PI * static_cast<float>(segment) / static_cast<float>(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);
            
            Vertex vertex;
            vertex.position.x = radius * sinPhi * cosTheta;
            vertex.position.y = radius * cosPhi;
            vertex.position.z = radius * sinPhi * sinTheta;
            
            vertex.normal = vertex.position.normalized();
            
            vertex.texCoord.x = static_cast<float>(segment) / static_cast<float>(segments);
            vertex.texCoord.y = static_cast<float>(ring) / static_cast<float>(rings);
            
            // Tangent
            vertex.tangent.x = -sinTheta;
            vertex.tangent.y = 0;
            vertex.tangent.z = cosTheta;
            vertex.tangent.normalize();
            
            // Bitangent
            vertex.bitangent = vertex.normal.cross(vertex.tangent);
            
            vertex.color = Math::Vector4(1, 1, 1, 1);
            
            vertices.push_back(vertex);
        }
    }
    
    // Generate indices (CLOCKWISE winding)
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t segment = 0; segment < segments; ++segment) {
            uint32_t current = ring * (segments + 1) + segment;
            uint32_t next = current + segments + 1;
            
            // CLOCKWISE from outside: current -> current+1 -> next
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);
            
            indices.push_back(current + 1);
            indices.push_back(next + 1);
            indices.push_back(next);
        }
    }
    
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreatePlane(float width, float height, uint32_t widthSegments, uint32_t heightSegments) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setName("Plane");
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    
    // Generate vertices
    for (uint32_t y = 0; y <= heightSegments; ++y) {
        for (uint32_t x = 0; x <= widthSegments; ++x) {
            Vertex vertex;
            
            float px = (static_cast<float>(x) / widthSegments) * width - halfWidth;
            float pz = (static_cast<float>(y) / heightSegments) * height - halfHeight;
            
            vertex.position = Math::Vector3(px, 0, pz);
            vertex.normal = Math::Vector3(0, 1, 0);
            vertex.texCoord = Math::Vector2(
                static_cast<float>(x) / widthSegments,
                static_cast<float>(y) / heightSegments
            );
            vertex.tangent = Math::Vector3(1, 0, 0);
            vertex.bitangent = Math::Vector3(0, 0, 1);
            vertex.color = Math::Vector4(1, 1, 1, 1);
            
            vertices.push_back(vertex);
        }
    }
    
    // Generate indices (CLOCKWISE winding)
    for (uint32_t y = 0; y < heightSegments; ++y) {
        for (uint32_t x = 0; x < widthSegments; ++x) {
            uint32_t current = y * (widthSegments + 1) + x;
            uint32_t next = current + widthSegments + 1;
            
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreateCylinder(float radius, float height, uint32_t segments) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setName("Cylinder");
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float halfHeight = height * 0.5f;
    
    // Side vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = 2.0f * Math::PI * static_cast<float>(i) / static_cast<float>(segments);
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        
        // Top vertex
        Vertex topVertex;
        topVertex.position = Math::Vector3(x, halfHeight, z);
        topVertex.normal = Math::Vector3(x, 0, z).normalized();
        topVertex.texCoord = Math::Vector2(static_cast<float>(i) / segments, 1);
        topVertex.color = Math::Vector4(1, 1, 1, 1);
        vertices.push_back(topVertex);
        
        // Bottom vertex
        Vertex bottomVertex;
        bottomVertex.position = Math::Vector3(x, -halfHeight, z);
        bottomVertex.normal = Math::Vector3(x, 0, z).normalized();
        bottomVertex.texCoord = Math::Vector2(static_cast<float>(i) / segments, 0);
        bottomVertex.color = Math::Vector4(1, 1, 1, 1);
        vertices.push_back(bottomVertex);
    }
    
    // Side indices (CLOCKWISE winding for grid-based mesh)
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t current = i * 2;
        uint32_t next = (i + 1) * 2;
        
        indices.push_back(current);
        indices.push_back(next);
        indices.push_back(current + 1);
        
        indices.push_back(current + 1);
        indices.push_back(next);
        indices.push_back(next + 1);
    }
    
    // Top and bottom caps
    uint32_t topCenterIndex = static_cast<uint32_t>(vertices.size());
    Vertex topCenter;
    topCenter.position = Math::Vector3(0, halfHeight, 0);
    topCenter.normal = Math::Vector3(0, 1, 0);
    topCenter.texCoord = Math::Vector2(0.5f, 0.5f);
    topCenter.color = Math::Vector4(1, 1, 1, 1);
    vertices.push_back(topCenter);
    
    uint32_t bottomCenterIndex = static_cast<uint32_t>(vertices.size());
    Vertex bottomCenter;
    bottomCenter.position = Math::Vector3(0, -halfHeight, 0);
    bottomCenter.normal = Math::Vector3(0, -1, 0);
    bottomCenter.texCoord = Math::Vector2(0.5f, 0.5f);
    bottomCenter.color = Math::Vector4(1, 1, 1, 1);
    vertices.push_back(bottomCenter);
    
    // Cap indices (CCW winding - CORRECT)
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t current = i * 2;
        uint32_t next = ((i + 1) % segments) * 2;
        
        // Top cap (looking down from +Y) - CCW
        indices.push_back(topCenterIndex);
        indices.push_back(next);
        indices.push_back(current);
        
        // Bottom cap (looking up from -Y) - CCW 
        indices.push_back(bottomCenterIndex);
        indices.push_back(current + 1);
        indices.push_back(next + 1);
    }
    
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    mesh->calculateTangents();
    
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreateCone(float radius, float height, uint32_t segments) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setName("Cone");
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Tip vertex
    Vertex tip;
    tip.position = Math::Vector3(0, height, 0);
    tip.normal = Math::Vector3(0, 1, 0);
    tip.texCoord = Math::Vector2(0.5f, 1);
    tip.color = Math::Vector4(1, 1, 1, 1);
    vertices.push_back(tip);
    
    // Base vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = 2.0f * Math::PI * static_cast<float>(i) / static_cast<float>(segments);
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        
        Vertex vertex;
        vertex.position = Math::Vector3(x, 0, z);
        
        // Calculate normal for cone side
        Math::Vector3 toTip = tip.position - vertex.position;
        Math::Vector3 tangent(-z, 0, x);
        vertex.normal = tangent.cross(toTip).normalized();
        
        vertex.texCoord = Math::Vector2(static_cast<float>(i) / segments, 0);
        vertex.color = Math::Vector4(1, 1, 1, 1);
        vertices.push_back(vertex);
    }
    
    // Side indices (CCW)
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
    }
    
    // Base center
    uint32_t baseCenterIndex = static_cast<uint32_t>(vertices.size());
    Vertex baseCenter;
    baseCenter.position = Math::Vector3(0, 0, 0);
    baseCenter.normal = Math::Vector3(0, -1, 0);
    baseCenter.texCoord = Math::Vector2(0.5f, 0.5f);
    baseCenter.color = Math::Vector4(1, 1, 1, 1);
    vertices.push_back(baseCenter);
    
    // Base indices (CCW winding)
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(baseCenterIndex);
        indices.push_back(i + 2);
        indices.push_back(i + 1);
    }
    
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreateTorus(float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setName("Torus");
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    for (uint32_t i = 0; i <= majorSegments; ++i) {
        float u = 2.0f * Math::PI * static_cast<float>(i) / static_cast<float>(majorSegments);
        float cosU = std::cos(u);
        float sinU = std::sin(u);
        
        for (uint32_t j = 0; j <= minorSegments; ++j) {
            float v = 2.0f * Math::PI * static_cast<float>(j) / static_cast<float>(minorSegments);
            float cosV = std::cos(v);
            float sinV = std::sin(v);
            
            Vertex vertex;
            vertex.position.x = (majorRadius + minorRadius * cosV) * cosU;
            vertex.position.y = minorRadius * sinV;
            vertex.position.z = (majorRadius + minorRadius * cosV) * sinU;
            
            Math::Vector3 center(majorRadius * cosU, 0, majorRadius * sinU);
            vertex.normal = (vertex.position - center).normalized();
            
            vertex.texCoord = Math::Vector2(
                static_cast<float>(i) / majorSegments,
                static_cast<float>(j) / minorSegments
            );
            
            vertex.color = Math::Vector4(1, 1, 1, 1);
            vertices.push_back(vertex);
        }
    }
    
    for (uint32_t i = 0; i < majorSegments; ++i) {
        for (uint32_t j = 0; j < minorSegments; ++j) {
            uint32_t current = i * (minorSegments + 1) + j;
            uint32_t next = (i + 1) * (minorSegments + 1) + j;
            
            // CLOCKWISE winding
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    mesh->calculateTangents();
    
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreateCapsule(float radius, float height, uint32_t segments) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setName("Capsule");
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float cylinderHeight = height - 2.0f * radius;
    float halfCylinderHeight = cylinderHeight * 0.5f;
    
    uint32_t rings = segments / 2;
    
    // Top hemisphere
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = (Math::PI * 0.5f) * static_cast<float>(ring) / static_cast<float>(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);
        
        for (uint32_t segment = 0; segment <= segments; ++segment) {
            float theta = 2.0f * Math::PI * static_cast<float>(segment) / static_cast<float>(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);
            
            Vertex vertex;
            vertex.position.x = radius * sinPhi * cosTheta;
            vertex.position.y = halfCylinderHeight + radius * cosPhi;
            vertex.position.z = radius * sinPhi * sinTheta;
            
            Math::Vector3 sphereCenter(0, halfCylinderHeight, 0);
            vertex.normal = (vertex.position - sphereCenter).normalized();
            
            vertex.texCoord = Math::Vector2(
                static_cast<float>(segment) / segments,
                0.5f + 0.5f * static_cast<float>(ring) / rings
            );
            
            vertex.color = Math::Vector4(1, 1, 1, 1);
            vertices.push_back(vertex);
        }
    }
    
    // Bottom hemisphere
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = (Math::PI * 0.5f) * static_cast<float>(ring) / static_cast<float>(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);
        
        for (uint32_t segment = 0; segment <= segments; ++segment) {
            float theta = 2.0f * Math::PI * static_cast<float>(segment) / static_cast<float>(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);
            
            Vertex vertex;
            vertex.position.x = radius * sinPhi * cosTheta;
            vertex.position.y = -halfCylinderHeight - radius * cosPhi;
            vertex.position.z = radius * sinPhi * sinTheta;
            
            Math::Vector3 sphereCenter(0, -halfCylinderHeight, 0);
            vertex.normal = (vertex.position - sphereCenter).normalized();
            
            vertex.texCoord = Math::Vector2(
                static_cast<float>(segment) / segments,
                0.5f - 0.5f * static_cast<float>(ring) / rings
            );
            
            vertex.color = Math::Vector4(1, 1, 1, 1);
            vertices.push_back(vertex);
        }
    }
    
    // Generate indices for both hemispheres
    uint32_t topOffset = 0;
    uint32_t bottomOffset = (rings + 1) * (segments + 1);
    
    for (uint32_t i = 0; i < rings * 2; ++i) {
        uint32_t offset = (i < rings) ? topOffset : (bottomOffset - (rings + 1) * (segments + 1));
        uint32_t row = (i < rings) ? i : (i - rings);
        
        for (uint32_t j = 0; j < segments; ++j) {
            uint32_t current = offset + row * (segments + 1) + j;
            uint32_t next = current + segments + 1;
            
            // CLOCKWISE winding
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    mesh->calculateTangents();
    
    return mesh;
}

} // namespace Crescent
