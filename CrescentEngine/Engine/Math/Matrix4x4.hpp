#pragma once

#include "Vector3.hpp"
#include "Vector4.hpp"
#include "Quaternion.hpp"
#include <cmath>
#include <ostream>
#include <cstring>
#if defined(__APPLE__)
#include <simd/simd.h>
#endif

namespace Crescent {
namespace Math {

struct Matrix4x4 {
    // Column-major order (like OpenGL/Metal)
    float m[16];
    
    // Constructors
    Matrix4x4() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f; // Identity
    }
    
    Matrix4x4(float diagonal) {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = diagonal;
    }
    
    Matrix4x4(float m00, float m01, float m02, float m03,
              float m10, float m11, float m12, float m13,
              float m20, float m21, float m22, float m23,
              float m30, float m31, float m32, float m33) {
        // Column-major order
        m[0] = m00; m[4] = m01; m[8]  = m02; m[12] = m03;
        m[1] = m10; m[5] = m11; m[9]  = m12; m[13] = m13;
        m[2] = m20; m[6] = m21; m[10] = m22; m[14] = m23;
        m[3] = m30; m[7] = m31; m[11] = m32; m[15] = m33;
    }
    
    // Static constants
    static const Matrix4x4 Identity;
    static const Matrix4x4 Zero;
    
    // Access operators
    float& operator[](int index) { return m[index]; }
    const float& operator[](int index) const { return m[index]; }
    
    float& operator()(int row, int col) { return m[col * 4 + row]; }
    const float& operator()(int row, int col) const { return m[col * 4 + row]; }

#if defined(__APPLE__)
private:
    static simd_float4x4 ToSIMD(const Matrix4x4& matrix) {
        simd_float4x4 result;
        result.columns[0] = simd_make_float4(matrix.m[0], matrix.m[1], matrix.m[2], matrix.m[3]);
        result.columns[1] = simd_make_float4(matrix.m[4], matrix.m[5], matrix.m[6], matrix.m[7]);
        result.columns[2] = simd_make_float4(matrix.m[8], matrix.m[9], matrix.m[10], matrix.m[11]);
        result.columns[3] = simd_make_float4(matrix.m[12], matrix.m[13], matrix.m[14], matrix.m[15]);
        return result;
    }

    static Matrix4x4 FromSIMD(const simd_float4x4& matrix) {
        Matrix4x4 result(0.0f);
        result.m[0] = matrix.columns[0].x; result.m[1] = matrix.columns[0].y;
        result.m[2] = matrix.columns[0].z; result.m[3] = matrix.columns[0].w;
        result.m[4] = matrix.columns[1].x; result.m[5] = matrix.columns[1].y;
        result.m[6] = matrix.columns[1].z; result.m[7] = matrix.columns[1].w;
        result.m[8] = matrix.columns[2].x; result.m[9] = matrix.columns[2].y;
        result.m[10] = matrix.columns[2].z; result.m[11] = matrix.columns[2].w;
        result.m[12] = matrix.columns[3].x; result.m[13] = matrix.columns[3].y;
        result.m[14] = matrix.columns[3].z; result.m[15] = matrix.columns[3].w;
        return result;
    }
#endif

public:
    
    // Matrix operations
    Matrix4x4 operator+(const Matrix4x4& other) const {
        Matrix4x4 result;
        for (int i = 0; i < 16; ++i)
            result.m[i] = m[i] + other.m[i];
        return result;
    }
    
    Matrix4x4 operator-(const Matrix4x4& other) const {
        Matrix4x4 result;
        for (int i = 0; i < 16; ++i)
            result.m[i] = m[i] - other.m[i];
        return result;
    }
    
    Matrix4x4 operator*(float scalar) const {
        Matrix4x4 result;
        for (int i = 0; i < 16; ++i)
            result.m[i] = m[i] * scalar;
        return result;
    }
    
    Matrix4x4 operator*(const Matrix4x4& other) const {
#if defined(__APPLE__)
        return FromSIMD(simd_mul(ToSIMD(*this), ToSIMD(other)));
#else
        Matrix4x4 result(0.0f);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                for (int k = 0; k < 4; ++k) {
                    result(i, j) += (*this)(i, k) * other(k, j);
                }
            }
        }
        return result;
#endif
    }
    
    Vector4 operator*(const Vector4& v) const {
#if defined(__APPLE__)
        simd_float4 result = simd_mul(ToSIMD(*this), simd_make_float4(v.x, v.y, v.z, v.w));
        return Vector4(result.x, result.y, result.z, result.w);
#else
        return Vector4(
            m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w
        );
#endif
    }
    
    Vector3 transformPoint(const Vector3& point) const {
#if defined(__APPLE__)
        simd_float4 v = simd_mul(ToSIMD(*this), simd_make_float4(point.x, point.y, point.z, 1.0f));
        if (std::abs(v.w) > 1e-8f) {
            float invW = 1.0f / v.w;
            return Vector3(v.x * invW, v.y * invW, v.z * invW);
        }
        return Vector3(v.x, v.y, v.z);
#else
        Vector4 v = (*this) * Vector4(point.x, point.y, point.z, 1.0f);
        if (v.w != 0.0f) {
            return Vector3(v.x / v.w, v.y / v.w, v.z / v.w);
        }
        return Vector3(v.x, v.y, v.z);
#endif
    }
    
    Vector3 transformDirection(const Vector3& dir) const {
#if defined(__APPLE__)
        simd_float4 v = simd_mul(ToSIMD(*this), simd_make_float4(dir.x, dir.y, dir.z, 0.0f));
        return Vector3(v.x, v.y, v.z);
#else
        Vector4 v = (*this) * Vector4(dir.x, dir.y, dir.z, 0.0f);
        return Vector3(v.x, v.y, v.z);
#endif
    }

    bool isAffine(float epsilon = 1e-6f) const {
        return std::abs(m[3]) <= epsilon &&
               std::abs(m[7]) <= epsilon &&
               std::abs(m[11]) <= epsilon &&
               std::abs(m[15] - 1.0f) <= epsilon;
    }

    Vector3 transformPointAffine(const Vector3& point) const {
        return Vector3(
            m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
            m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
            m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]
        );
    }

    Matrix4x4 inversedAffine() const {
        if (!isAffine()) {
            return inversed();
        }

#if defined(__APPLE__)
        simd_float3x3 linear;
        linear.columns[0] = simd_make_float3(m[0], m[1], m[2]);
        linear.columns[1] = simd_make_float3(m[4], m[5], m[6]);
        linear.columns[2] = simd_make_float3(m[8], m[9], m[10]);

        const float det = simd_determinant(linear);
        if (std::abs(det) < 1e-8f) {
            return Identity;
        }

        const simd_float3x3 invLinear = simd_inverse(linear);
        const simd_float3 translation = simd_make_float3(m[12], m[13], m[14]);
        const simd_float3 invTranslation = -simd_mul(invLinear, translation);

        Matrix4x4 result(1.0f);
        result.m[0] = invLinear.columns[0].x; result.m[1] = invLinear.columns[0].y; result.m[2] = invLinear.columns[0].z; result.m[3] = 0.0f;
        result.m[4] = invLinear.columns[1].x; result.m[5] = invLinear.columns[1].y; result.m[6] = invLinear.columns[1].z; result.m[7] = 0.0f;
        result.m[8] = invLinear.columns[2].x; result.m[9] = invLinear.columns[2].y; result.m[10] = invLinear.columns[2].z; result.m[11] = 0.0f;
        result.m[12] = invTranslation.x; result.m[13] = invTranslation.y; result.m[14] = invTranslation.z; result.m[15] = 1.0f;
        return result;
#else
        const float a = m[0], b = m[4], c = m[8];
        const float d = m[1], e = m[5], f = m[9];
        const float g = m[2], h = m[6], i = m[10];

        const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        if (std::abs(det) < 1e-8f) {
            return Identity;
        }

        const float invDet = 1.0f / det;
        const float r00 = (e * i - f * h) * invDet;
        const float r01 = (c * h - b * i) * invDet;
        const float r02 = (b * f - c * e) * invDet;
        const float r10 = (f * g - d * i) * invDet;
        const float r11 = (a * i - c * g) * invDet;
        const float r12 = (c * d - a * f) * invDet;
        const float r20 = (d * h - e * g) * invDet;
        const float r21 = (b * g - a * h) * invDet;
        const float r22 = (a * e - b * d) * invDet;

        const float tx = m[12];
        const float ty = m[13];
        const float tz = m[14];

        Matrix4x4 result(1.0f);
        result.m[0] = r00; result.m[1] = r10; result.m[2] = r20; result.m[3] = 0.0f;
        result.m[4] = r01; result.m[5] = r11; result.m[6] = r21; result.m[7] = 0.0f;
        result.m[8] = r02; result.m[9] = r12; result.m[10] = r22; result.m[11] = 0.0f;
        result.m[12] = -(r00 * tx + r01 * ty + r02 * tz);
        result.m[13] = -(r10 * tx + r11 * ty + r12 * tz);
        result.m[14] = -(r20 * tx + r21 * ty + r22 * tz);
        result.m[15] = 1.0f;
        return result;
#endif
    }

    Matrix4x4 normalMatrix() const {
        if (!isAffine()) {
            return inversed().transposed();
        }

        Matrix4x4 result = inversedAffine().transposed();
        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[3] = 0.0f;
        result.m[7] = 0.0f;
        result.m[11] = 0.0f;
        result.m[15] = 1.0f;
        return result;
    }

    void transformAABB(const Vector3& localMin,
                       const Vector3& localMax,
                       Vector3& outMin,
                       Vector3& outMax) const {
        if (!isAffine()) {
            const Vector3 corners[8] = {
                Vector3(localMin.x, localMin.y, localMin.z),
                Vector3(localMax.x, localMin.y, localMin.z),
                Vector3(localMin.x, localMax.y, localMin.z),
                Vector3(localMax.x, localMax.y, localMin.z),
                Vector3(localMin.x, localMin.y, localMax.z),
                Vector3(localMax.x, localMin.y, localMax.z),
                Vector3(localMin.x, localMax.y, localMax.z),
                Vector3(localMax.x, localMax.y, localMax.z)
            };

            outMin = transformPoint(corners[0]);
            outMax = outMin;
            for (size_t i = 1; i < 8; ++i) {
                Vector3 worldCorner = transformPoint(corners[i]);
                outMin = Vector3::Min(outMin, worldCorner);
                outMax = Vector3::Max(outMax, worldCorner);
            }
            return;
        }

        const Vector3 center = (localMin + localMax) * 0.5f;
        const Vector3 extents = (localMax - localMin) * 0.5f;
        const Vector3 worldCenter = transformPointAffine(center);
        const Vector3 worldExtents(
            std::abs(m[0]) * extents.x + std::abs(m[4]) * extents.y + std::abs(m[8]) * extents.z,
            std::abs(m[1]) * extents.x + std::abs(m[5]) * extents.y + std::abs(m[9]) * extents.z,
            std::abs(m[2]) * extents.x + std::abs(m[6]) * extents.y + std::abs(m[10]) * extents.z
        );
        outMin = worldCenter - worldExtents;
        outMax = worldCenter + worldExtents;
    }
    
    // Transpose
    Matrix4x4 transposed() const {
#if defined(__APPLE__)
        return FromSIMD(simd_transpose(ToSIMD(*this)));
#else
        Matrix4x4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result(i, j) = (*this)(j, i);
            }
        }
        return result;
#endif
    }
    
    void transpose() {
        *this = transposed();
    }
    
    // Determinant
    float determinant() const {
        float a = m[0], b = m[4], c = m[8],  d = m[12];
        float e = m[1], f = m[5], g = m[9],  h = m[13];
        float i = m[2], j = m[6], k = m[10], l = m[14];
        float n = m[3], o = m[7], p = m[11], q = m[15];
        
        return (a * f - b * e) * (k * q - l * p) -
               (a * g - c * e) * (j * q - l * o) +
               (a * h - d * e) * (j * p - k * o) +
               (b * g - c * f) * (i * q - l * n) -
               (b * h - d * f) * (i * p - k * n) +
               (c * h - d * g) * (i * o - j * n);
    }
    
    // Inverse
    Matrix4x4 inversed() const {
#if defined(__APPLE__)
        return FromSIMD(simd_inverse(ToSIMD(*this)));
#else
        float det = determinant();
        if (std::abs(det) < 1e-6f) {
            return Identity;
        }
        
        Matrix4x4 inv;
        float invDet = 1.0f / det;
        
        inv.m[0] = (m[5] * (m[10] * m[15] - m[11] * m[14]) - 
                    m[9] * (m[6] * m[15] - m[7] * m[14]) + 
                    m[13] * (m[6] * m[11] - m[7] * m[10])) * invDet;
        
        inv.m[1] = -(m[1] * (m[10] * m[15] - m[11] * m[14]) - 
                     m[9] * (m[2] * m[15] - m[3] * m[14]) + 
                     m[13] * (m[2] * m[11] - m[3] * m[10])) * invDet;
        
        inv.m[2] = (m[1] * (m[6] * m[15] - m[7] * m[14]) - 
                    m[5] * (m[2] * m[15] - m[3] * m[14]) + 
                    m[13] * (m[2] * m[7] - m[3] * m[6])) * invDet;
        
        inv.m[3] = -(m[1] * (m[6] * m[11] - m[7] * m[10]) - 
                     m[5] * (m[2] * m[11] - m[3] * m[10]) + 
                     m[9] * (m[2] * m[7] - m[3] * m[6])) * invDet;
        
        inv.m[4] = -(m[4] * (m[10] * m[15] - m[11] * m[14]) - 
                     m[8] * (m[6] * m[15] - m[7] * m[14]) + 
                     m[12] * (m[6] * m[11] - m[7] * m[10])) * invDet;
        
        inv.m[5] = (m[0] * (m[10] * m[15] - m[11] * m[14]) - 
                    m[8] * (m[2] * m[15] - m[3] * m[14]) + 
                    m[12] * (m[2] * m[11] - m[3] * m[10])) * invDet;
        
        inv.m[6] = -(m[0] * (m[6] * m[15] - m[7] * m[14]) - 
                     m[4] * (m[2] * m[15] - m[3] * m[14]) + 
                     m[12] * (m[2] * m[7] - m[3] * m[6])) * invDet;
        
        inv.m[7] = (m[0] * (m[6] * m[11] - m[7] * m[10]) - 
                    m[4] * (m[2] * m[11] - m[3] * m[10]) + 
                    m[8] * (m[2] * m[7] - m[3] * m[6])) * invDet;
        
        inv.m[8] = (m[4] * (m[9] * m[15] - m[11] * m[13]) - 
                    m[8] * (m[5] * m[15] - m[7] * m[13]) + 
                    m[12] * (m[5] * m[11] - m[7] * m[9])) * invDet;
        
        inv.m[9] = -(m[0] * (m[9] * m[15] - m[11] * m[13]) - 
                     m[8] * (m[1] * m[15] - m[3] * m[13]) + 
                     m[12] * (m[1] * m[11] - m[3] * m[9])) * invDet;
        
        inv.m[10] = (m[0] * (m[5] * m[15] - m[7] * m[13]) - 
                     m[4] * (m[1] * m[15] - m[3] * m[13]) + 
                     m[12] * (m[1] * m[7] - m[3] * m[5])) * invDet;
        
        inv.m[11] = -(m[0] * (m[5] * m[11] - m[7] * m[9]) - 
                      m[4] * (m[1] * m[11] - m[3] * m[9]) + 
                      m[8] * (m[1] * m[7] - m[3] * m[5])) * invDet;
        
        inv.m[12] = -(m[4] * (m[9] * m[14] - m[10] * m[13]) - 
                      m[8] * (m[5] * m[14] - m[6] * m[13]) + 
                      m[12] * (m[5] * m[10] - m[6] * m[9])) * invDet;
        
        inv.m[13] = (m[0] * (m[9] * m[14] - m[10] * m[13]) - 
                     m[8] * (m[1] * m[14] - m[2] * m[13]) + 
                     m[12] * (m[1] * m[10] - m[2] * m[9])) * invDet;
        
        inv.m[14] = -(m[0] * (m[5] * m[14] - m[6] * m[13]) - 
                      m[4] * (m[1] * m[14] - m[2] * m[13]) + 
                      m[12] * (m[1] * m[6] - m[2] * m[5])) * invDet;
        
        inv.m[15] = (m[0] * (m[5] * m[10] - m[6] * m[9]) - 
                     m[4] * (m[1] * m[10] - m[2] * m[9]) + 
                     m[8] * (m[1] * m[6] - m[2] * m[5])) * invDet;
        
        return inv;
#endif
    }
    
    // Static transformation matrices
    static Matrix4x4 Translate(const Vector3& translation) {
        Matrix4x4 result;
        result.m[12] = translation.x;
        result.m[13] = translation.y;
        result.m[14] = translation.z;
        return result;
    }
    
    static Matrix4x4 Scale(const Vector3& scale) {
        Matrix4x4 result(0.0f);
        result.m[0] = scale.x;
        result.m[5] = scale.y;
        result.m[10] = scale.z;
        result.m[15] = 1.0f;
        return result;
    }
    
    static Matrix4x4 RotateX(float angle) {
        Matrix4x4 result;
        float c = std::cos(angle);
        float s = std::sin(angle);
        result.m[5] = c;
        result.m[6] = s;
        result.m[9] = -s;
        result.m[10] = c;
        return result;
    }
    
    static Matrix4x4 RotateY(float angle) {
        Matrix4x4 result;
        float c = std::cos(angle);
        float s = std::sin(angle);
        result.m[0] = c;
        result.m[2] = -s;
        result.m[8] = s;
        result.m[10] = c;
        return result;
    }
    
    static Matrix4x4 RotateZ(float angle) {
        Matrix4x4 result;
        float c = std::cos(angle);
        float s = std::sin(angle);
        result.m[0] = c;
        result.m[1] = s;
        result.m[4] = -s;
        result.m[5] = c;
        return result;
    }
    
    static Matrix4x4 Rotate(const Quaternion& rotation) {
        Matrix4x4 result;
        
        float xx = rotation.x * rotation.x;
        float yy = rotation.y * rotation.y;
        float zz = rotation.z * rotation.z;
        float xy = rotation.x * rotation.y;
        float xz = rotation.x * rotation.z;
        float yz = rotation.y * rotation.z;
        float wx = rotation.w * rotation.x;
        float wy = rotation.w * rotation.y;
        float wz = rotation.w * rotation.z;
        
        result.m[0] = 1.0f - 2.0f * (yy + zz);
        result.m[1] = 2.0f * (xy + wz);
        result.m[2] = 2.0f * (xz - wy);
        result.m[3] = 0.0f;
        
        result.m[4] = 2.0f * (xy - wz);
        result.m[5] = 1.0f - 2.0f * (xx + zz);
        result.m[6] = 2.0f * (yz + wx);
        result.m[7] = 0.0f;
        
        result.m[8] = 2.0f * (xz + wy);
        result.m[9] = 2.0f * (yz - wx);
        result.m[10] = 1.0f - 2.0f * (xx + yy);
        result.m[11] = 0.0f;
        
        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[15] = 1.0f;
        
        return result;
    }
    
    static Matrix4x4 TRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale) {
        return Translate(translation) * Rotate(rotation) * Scale(scale);
    }
    
    // ==========================================================================
    // PROJECTION MATRICES - Right-handed coordinate system, Metal NDC (Z: [0,1])
    // ==========================================================================
    // 
    // Convention:
    // - Right-handed: camera looks down -Z axis, +X is right, +Y is up
    // - Metal NDC: X,Y in [-1,1], Z in [0,1] (not [-1,1] like OpenGL)
    // - Objects in front of camera have negative view-space Z
    // ==========================================================================
    
    static Matrix4x4 Perspective(float fov, float aspect, float nearZ, float farZ) {
        // Right-handed perspective projection for Metal (Z maps to [0,1])
        // Camera looks down -Z, so visible objects have negative Z in view space
        // After projection: near plane maps to Z=0, far plane maps to Z=1
        Matrix4x4 result(0.0f);
        float tanHalfFov = std::tan(fov * 0.5f);
        
        result.m[0] = 1.0f / (aspect * tanHalfFov);
        result.m[5] = 1.0f / tanHalfFov;
        result.m[10] = farZ / (nearZ - farZ);           // Maps -near to 0, -far to 1
        result.m[11] = -1.0f;                           // Right-handed: w = -z
        result.m[14] = (farZ * nearZ) / (nearZ - farZ); // Depth offset
        
        return result;
    }
    
    static Matrix4x4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) {
        // Right-handed orthographic projection for Metal (Z maps to [0,1])
        Matrix4x4 result(0.0f);
        
        result.m[0] = 2.0f / (right - left);
        result.m[5] = 2.0f / (top - bottom);
        result.m[10] = 1.0f / (nearZ - farZ);           // Right-handed: flip Z
        result.m[12] = -(right + left) / (right - left);
        result.m[13] = -(top + bottom) / (top - bottom);
        result.m[14] = nearZ / (nearZ - farZ);          // Maps near to 0
        result.m[15] = 1.0f;
        
        return result;
    }
    
    // ==========================================================================
    // VIEW MATRIX - Right-handed coordinate system
    // ==========================================================================
    
    static Matrix4x4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
        // Right-handed LookAt: camera looks down -Z axis
        // Forward vector points FROM camera TO target, but we store -forward in Z row
        Vector3 f = (target - eye).normalized();  // Forward direction
        Vector3 r = f.cross(up).normalized();     // Right = forward x up
        Vector3 u = r.cross(f);                   // True up = right x forward
        
        Matrix4x4 result;
        // First column: right vector
        result.m[0] = r.x;
        result.m[1] = u.x;
        result.m[2] = -f.x;  // Negate for right-handed (camera looks down -Z)
        result.m[3] = 0.0f;
        
        // Second column: up vector  
        result.m[4] = r.y;
        result.m[5] = u.y;
        result.m[6] = -f.y;
        result.m[7] = 0.0f;
        
        // Third column: -forward vector (right-handed)
        result.m[8] = r.z;
        result.m[9] = u.z;
        result.m[10] = -f.z;
        result.m[11] = 0.0f;
        
        // Fourth column: translation
        result.m[12] = -r.dot(eye);
        result.m[13] = -u.dot(eye);
        result.m[14] = f.dot(eye);   // Positive because we negated f above
        result.m[15] = 1.0f;
        
        return result;
    }
    
    // Stream output
    friend std::ostream& operator<<(std::ostream& os, const Matrix4x4& mat) {
        os << "Matrix4x4(\n";
        for (int i = 0; i < 4; ++i) {
            os << "  [";
            for (int j = 0; j < 4; ++j) {
                os << mat(i, j);
                if (j < 3) os << ", ";
            }
            os << "]\n";
        }
        os << ")";
        return os;
    }
};

// Static member definitions
inline const Matrix4x4 Matrix4x4::Identity = Matrix4x4();
inline const Matrix4x4 Matrix4x4::Zero = Matrix4x4(0.0f);

// Scalar multiplication (scalar * matrix)
inline Matrix4x4 operator*(float scalar, const Matrix4x4& mat) { return mat * scalar; }

} // namespace Math
} // namespace Crescent
