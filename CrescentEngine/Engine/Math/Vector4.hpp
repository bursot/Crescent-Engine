#pragma once

#include <cmath>
#include <ostream>

namespace Crescent {
namespace Math {

struct Vector4 {
    float x, y, z, w;
    
    // Constructors
    Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vector4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    
    // Static constants
    static const Vector4 Zero;
    static const Vector4 One;
    static const Vector4 UnitX;
    static const Vector4 UnitY;
    static const Vector4 UnitZ;
    static const Vector4 UnitW;
    
    // Arithmetic operators
    Vector4 operator+(const Vector4& other) const { 
        return Vector4(x + other.x, y + other.y, z + other.z, w + other.w); 
    }
    Vector4 operator-(const Vector4& other) const { 
        return Vector4(x - other.x, y - other.y, z - other.z, w - other.w); 
    }
    Vector4 operator*(float scalar) const { 
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar); 
    }
    Vector4 operator/(float scalar) const { 
        return Vector4(x / scalar, y / scalar, z / scalar, w / scalar); 
    }
    Vector4 operator*(const Vector4& other) const { 
        return Vector4(x * other.x, y * other.y, z * other.z, w * other.w); 
    }
    
    Vector4& operator+=(const Vector4& other) { 
        x += other.x; y += other.y; z += other.z; w += other.w; 
        return *this; 
    }
    Vector4& operator-=(const Vector4& other) { 
        x -= other.x; y -= other.y; z -= other.z; w -= other.w; 
        return *this; 
    }
    Vector4& operator*=(float scalar) { 
        x *= scalar; y *= scalar; z *= scalar; w *= scalar; 
        return *this; 
    }
    Vector4& operator/=(float scalar) { 
        x /= scalar; y /= scalar; z /= scalar; w /= scalar; 
        return *this; 
    }
    Vector4& operator*=(const Vector4& other) { 
        x *= other.x; y *= other.y; z *= other.z; w *= other.w; 
        return *this; 
    }
    
    Vector4 operator-() const { return Vector4(-x, -y, -z, -w); }
    
    // Comparison operators
    bool operator==(const Vector4& other) const { 
        return x == other.x && y == other.y && z == other.z && w == other.w; 
    }
    bool operator!=(const Vector4& other) const { return !(*this == other); }
    
    // Array access
    float& operator[](int index) { return (&x)[index]; }
    const float& operator[](int index) const { return (&x)[index]; }
    
    // Vector operations
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float lengthSquared() const { return x * x + y * y + z * z + w * w; }
    
    Vector4 normalized() const {
        float len = length();
        return (len > 0.0f) ? (*this / len) : Vector4::Zero;
    }
    
    void normalize() {
        float len = length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
            z /= len;
            w /= len;
        }
    }
    
    float dot(const Vector4& other) const { 
        return x * other.x + y * other.y + z * other.z + w * other.w; 
    }
    
    float distance(const Vector4& other) const { return (*this - other).length(); }
    float distanceSquared(const Vector4& other) const { return (*this - other).lengthSquared(); }
    
    // Static utility functions
    static float Dot(const Vector4& a, const Vector4& b) { return a.dot(b); }
    static float Distance(const Vector4& a, const Vector4& b) { return a.distance(b); }
    static Vector4 Lerp(const Vector4& a, const Vector4& b, float t) { return a + (b - a) * t; }
    static Vector4 Min(const Vector4& a, const Vector4& b) { 
        return Vector4(
            std::min(a.x, b.x), std::min(a.y, b.y), 
            std::min(a.z, b.z), std::min(a.w, b.w)
        ); 
    }
    static Vector4 Max(const Vector4& a, const Vector4& b) { 
        return Vector4(
            std::max(a.x, b.x), std::max(a.y, b.y), 
            std::max(a.z, b.z), std::max(a.w, b.w)
        ); 
    }
    
    // Stream output
    friend std::ostream& operator<<(std::ostream& os, const Vector4& v) {
        os << "Vector4(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
        return os;
    }
};

// Static member definitions
inline const Vector4 Vector4::Zero = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
inline const Vector4 Vector4::One = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
inline const Vector4 Vector4::UnitX = Vector4(1.0f, 0.0f, 0.0f, 0.0f);
inline const Vector4 Vector4::UnitY = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
inline const Vector4 Vector4::UnitZ = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
inline const Vector4 Vector4::UnitW = Vector4(0.0f, 0.0f, 0.0f, 1.0f);

// Scalar multiplication (scalar * vector)
inline Vector4 operator*(float scalar, const Vector4& v) { return v * scalar; }

} // namespace Math
} // namespace Crescent
