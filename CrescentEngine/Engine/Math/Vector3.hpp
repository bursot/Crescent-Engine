#pragma once

#include <cmath>
#include <ostream>

namespace Crescent {
namespace Math {

struct Vector3 {
    float x, y, z;
    
    // Constructors
    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vector3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    // Static constants
    static const Vector3 Zero;
    static const Vector3 One;
    static const Vector3 UnitX;
    static const Vector3 UnitY;
    static const Vector3 UnitZ;
    static const Vector3 Forward;
    static const Vector3 Back;
    static const Vector3 Up;
    static const Vector3 Down;
    static const Vector3 Right;
    static const Vector3 Left;
    
    // Arithmetic operators
    Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z + other.z); }
    Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }
    Vector3 operator*(float scalar) const { return Vector3(x * scalar, y * scalar, z * scalar); }
    Vector3 operator/(float scalar) const { return Vector3(x / scalar, y / scalar, z / scalar); }
    Vector3 operator*(const Vector3& other) const { return Vector3(x * other.x, y * other.y, z * other.z); }
    
    Vector3& operator+=(const Vector3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vector3& operator-=(const Vector3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vector3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vector3& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }
    Vector3& operator*=(const Vector3& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
    
    Vector3 operator-() const { return Vector3(-x, -y, -z); }
    
    // Comparison operators
    bool operator==(const Vector3& other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const Vector3& other) const { return !(*this == other); }
    
    // Array access
    float& operator[](int index) { return (&x)[index]; }
    const float& operator[](int index) const { return (&x)[index]; }
    
    // Vector operations
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSquared() const { return x * x + y * y + z * z; }
    
    Vector3 normalized() const {
        float len = length();
        return (len > 0.0f) ? (*this / len) : Vector3::Zero;
    }
    
    void normalize() {
        float len = length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
            z /= len;
        }
    }
    
    float dot(const Vector3& other) const { return x * other.x + y * other.y + z * other.z; }
    
    Vector3 cross(const Vector3& other) const {
        return Vector3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }
    
    float distance(const Vector3& other) const { return (*this - other).length(); }
    float distanceSquared(const Vector3& other) const { return (*this - other).lengthSquared(); }
    
    float angleTo(const Vector3& other) const {
        float d = dot(other);
        float lenProduct = length() * other.length();
        return (lenProduct > 0.0f) ? std::acos(d / lenProduct) : 0.0f;
    }
    
    Vector3 reflect(const Vector3& normal) const {
        return *this - normal * (2.0f * dot(normal));
    }
    
    Vector3 project(const Vector3& other) const {
        float d = other.dot(other);
        return (d > 0.0f) ? other * (dot(other) / d) : Vector3::Zero;
    }
    
    // Static utility functions
    static float Dot(const Vector3& a, const Vector3& b) { return a.dot(b); }
    static Vector3 Cross(const Vector3& a, const Vector3& b) { return a.cross(b); }
    static float Distance(const Vector3& a, const Vector3& b) { return a.distance(b); }
    static float Angle(const Vector3& a, const Vector3& b) { return a.angleTo(b); }
    static Vector3 Lerp(const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }
    static Vector3 Slerp(const Vector3& a, const Vector3& b, float t);
    static Vector3 Min(const Vector3& a, const Vector3& b) { 
        return Vector3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); 
    }
    static Vector3 Max(const Vector3& a, const Vector3& b) { 
        return Vector3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); 
    }
    static Vector3 Clamp(const Vector3& v, const Vector3& min, const Vector3& max) {
        return Vector3(
            std::clamp(v.x, min.x, max.x),
            std::clamp(v.y, min.y, max.y),
            std::clamp(v.z, min.z, max.z)
        );
    }
    
    // Stream output
    friend std::ostream& operator<<(std::ostream& os, const Vector3& v) {
        os << "Vector3(" << v.x << ", " << v.y << ", " << v.z << ")";
        return os;
    }
};

// Static member definitions
inline const Vector3 Vector3::Zero = Vector3(0.0f, 0.0f, 0.0f);
inline const Vector3 Vector3::One = Vector3(1.0f, 1.0f, 1.0f);
inline const Vector3 Vector3::UnitX = Vector3(1.0f, 0.0f, 0.0f);
inline const Vector3 Vector3::UnitY = Vector3(0.0f, 1.0f, 0.0f);
inline const Vector3 Vector3::UnitZ = Vector3(0.0f, 0.0f, 1.0f);
inline const Vector3 Vector3::Forward = Vector3(0.0f, 0.0f, -1.0f);
inline const Vector3 Vector3::Back = Vector3(0.0f, 0.0f, 1.0f);
inline const Vector3 Vector3::Up = Vector3(0.0f, 1.0f, 0.0f);
inline const Vector3 Vector3::Down = Vector3(0.0f, -1.0f, 0.0f);
inline const Vector3 Vector3::Right = Vector3(1.0f, 0.0f, 0.0f);
inline const Vector3 Vector3::Left = Vector3(-1.0f, 0.0f, 0.0f);

// Scalar multiplication (scalar * vector)
inline Vector3 operator*(float scalar, const Vector3& v) { return v * scalar; }

// Slerp implementation
inline Vector3 Vector3::Slerp(const Vector3& a, const Vector3& b, float t) {
    float angle = a.angleTo(b);
    if (angle < 0.001f) return Lerp(a, b, t);
    
    float sinAngle = std::sin(angle);
    float ratioA = std::sin((1.0f - t) * angle) / sinAngle;
    float ratioB = std::sin(t * angle) / sinAngle;
    
    return a * ratioA + b * ratioB;
}

} // namespace Math
} // namespace Crescent
