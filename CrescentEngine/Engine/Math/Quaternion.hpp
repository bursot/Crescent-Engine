#pragma once

#include "Vector3.hpp"
#include <cmath>
#include <ostream>

namespace Crescent {
namespace Math {

// Forward declaration
struct Matrix4x4;

struct Quaternion {
    float x, y, z, w;
    
    // Constructors
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    
    // Static constants
    static const Quaternion Identity;
    
    // Arithmetic operators
    Quaternion operator+(const Quaternion& other) const {
        return Quaternion(x + other.x, y + other.y, z + other.z, w + other.w);
    }
    
    Quaternion operator-(const Quaternion& other) const {
        return Quaternion(x - other.x, y - other.y, z - other.z, w - other.w);
    }
    
    Quaternion operator*(float scalar) const {
        return Quaternion(x * scalar, y * scalar, z * scalar, w * scalar);
    }
    
    Quaternion operator*(const Quaternion& other) const {
        return Quaternion(
            w * other.x + x * other.w + y * other.z - z * other.y,
            w * other.y + y * other.w + z * other.x - x * other.z,
            w * other.z + z * other.w + x * other.y - y * other.x,
            w * other.w - x * other.x - y * other.y - z * other.z
        );
    }
    
    Vector3 operator*(const Vector3& v) const {
        Vector3 qvec(x, y, z);
        Vector3 uv = qvec.cross(v);
        Vector3 uuv = qvec.cross(uv);
        return v + ((uv * w) + uuv) * 2.0f;
    }
    
    Quaternion& operator*=(const Quaternion& other) {
        *this = *this * other;
        return *this;
    }
    
    Quaternion& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar; w *= scalar;
        return *this;
    }
    
    // Comparison
    bool operator==(const Quaternion& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
    bool operator!=(const Quaternion& other) const { return !(*this == other); }
    
    // Quaternion operations
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float lengthSquared() const { return x * x + y * y + z * z + w * w; }
    
    Quaternion normalized() const {
        float len = length();
        return (len > 0.0f) ? (*this * (1.0f / len)) : Identity;
    }
    
    void normalize() {
        float len = length();
        if (len > 0.0f) {
            float invLen = 1.0f / len;
            x *= invLen;
            y *= invLen;
            z *= invLen;
            w *= invLen;
        }
    }
    
    Quaternion conjugate() const {
        return Quaternion(-x, -y, -z, w);
    }
    
    Quaternion inverse() const {
        float lenSq = lengthSquared();
        if (lenSq > 0.0f) {
            return conjugate() * (1.0f / lenSq);
        }
        return Identity;
    }
    
    float dot(const Quaternion& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }
    
    // Convert to Euler angles (in radians)
    Vector3 toEulerAngles() const {
        Vector3 angles;
        
        // Roll (x-axis rotation)
        float sinr_cosp = 2.0f * (w * x + y * z);
        float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        angles.x = std::atan2(sinr_cosp, cosr_cosp);
        
        // Pitch (y-axis rotation)
        float sinp = 2.0f * (w * y - z * x);
        if (std::abs(sinp) >= 1.0f)
            angles.y = std::copysign(M_PI / 2.0f, sinp);
        else
            angles.y = std::asin(sinp);
        
        // Yaw (z-axis rotation)
        float siny_cosp = 2.0f * (w * z + x * y);
        float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        angles.z = std::atan2(siny_cosp, cosy_cosp);
        
        return angles;
    }
    
    // Static construction methods
    static Quaternion FromEulerAngles(const Vector3& euler) {
        float cx = std::cos(euler.x * 0.5f);
        float sx = std::sin(euler.x * 0.5f);
        float cy = std::cos(euler.y * 0.5f);
        float sy = std::sin(euler.y * 0.5f);
        float cz = std::cos(euler.z * 0.5f);
        float sz = std::sin(euler.z * 0.5f);
        
        Quaternion q;
        q.w = cx * cy * cz + sx * sy * sz;
        q.x = sx * cy * cz - cx * sy * sz;
        q.y = cx * sy * cz + sx * cy * sz;
        q.z = cx * cy * sz - sx * sy * cz;
        
        return q;
    }
    
    static Quaternion FromAxisAngle(const Vector3& axis, float angle) {
        float halfAngle = angle * 0.5f;
        float s = std::sin(halfAngle);
        Vector3 normalizedAxis = axis.normalized();
        
        return Quaternion(
            normalizedAxis.x * s,
            normalizedAxis.y * s,
            normalizedAxis.z * s,
            std::cos(halfAngle)
        );
    }
    
    static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3::Up) {
        // Right-handed: forward is the direction to look at (will become -Z)
        Vector3 f = forward.normalized();
        Vector3 r = f.cross(up).normalized();  // Right = forward x up
        Vector3 u = r.cross(f);                // True up = right x forward
        
        // Build rotation matrix and convert to quaternion
        // Note: f goes into -Z, so we use -f for the Z column
        float trace = r.x + u.y + (-f.z);
        Quaternion q;
        
        if (trace > 0.0f) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (u.z - (-f.y)) / s;  // u.z - (-f).y
            q.y = ((-f.x) - r.z) / s;  // (-f).x - r.z  
            q.z = (r.y - u.x) / s;
        } else if (r.x > u.y && r.x > (-f.z)) {
            float s = std::sqrt(1.0f + r.x - u.y - (-f.z)) * 2.0f;
            q.w = (u.z - (-f.y)) / s;
            q.x = 0.25f * s;
            q.y = (u.x + r.y) / s;
            q.z = ((-f.x) + r.z) / s;
        } else if (u.y > (-f.z)) {
            float s = std::sqrt(1.0f + u.y - r.x - (-f.z)) * 2.0f;
            q.w = ((-f.x) - r.z) / s;
            q.x = (u.x + r.y) / s;
            q.y = 0.25f * s;
            q.z = ((-f.y) + u.z) / s;
        } else {
            float s = std::sqrt(1.0f + (-f.z) - r.x - u.y) * 2.0f;
            q.w = (r.y - u.x) / s;
            q.x = ((-f.x) + r.z) / s;
            q.y = ((-f.y) + u.z) / s;
            q.z = 0.25f * s;
        }
        
        return q;
    }
    
    // Interpolation
    static Quaternion Lerp(const Quaternion& a, const Quaternion& b, float t) {
        return (a + (b - a) * t).normalized();
    }
    
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
        Quaternion qa = a;
        Quaternion qb = b;
        
        float cosTheta = qa.dot(qb);
        
        // If negative dot, negate one quaternion to take shorter path
        if (cosTheta < 0.0f) {
            qb = qb * -1.0f;
            cosTheta = -cosTheta;
        }
        
        // Use linear interpolation if quaternions are very close
        if (cosTheta > 0.9995f) {
            return Lerp(qa, qb, t);
        }
        
        float theta = std::acos(cosTheta);
        float sinTheta = std::sin(theta);
        
        float wa = std::sin((1.0f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;
        
        return qa * wa + qb * wb;
    }
    
    static float Dot(const Quaternion& a, const Quaternion& b) { return a.dot(b); }
    
    // Stream output
    friend std::ostream& operator<<(std::ostream& os, const Quaternion& q) {
        os << "Quaternion(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")";
        return os;
    }
};

// Static member definitions
inline const Quaternion Quaternion::Identity = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);

// Scalar multiplication (scalar * quaternion)
inline Quaternion operator*(float scalar, const Quaternion& q) { return q * scalar; }

} // namespace Math
} // namespace Crescent
