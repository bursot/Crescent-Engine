#pragma once

#include <cmath>
#include <ostream>

namespace Crescent {
namespace Math {

struct Vector2 {
    float x, y;
    
    // Constructors
    Vector2() : x(0.0f), y(0.0f) {}
    Vector2(float scalar) : x(scalar), y(scalar) {}
    Vector2(float x, float y) : x(x), y(y) {}
    
    // Static constants
    static const Vector2 Zero;
    static const Vector2 One;
    static const Vector2 UnitX;
    static const Vector2 UnitY;
    
    // Arithmetic operators
    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    Vector2 operator/(float scalar) const { return Vector2(x / scalar, y / scalar); }
    
    Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }
    Vector2& operator-=(const Vector2& other) { x -= other.x; y -= other.y; return *this; }
    Vector2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
    Vector2& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }
    
    Vector2 operator-() const { return Vector2(-x, -y); }
    
    // Comparison operators
    bool operator==(const Vector2& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Vector2& other) const { return !(*this == other); }
    
    // Array access
    float& operator[](int index) { return (&x)[index]; }
    const float& operator[](int index) const { return (&x)[index]; }
    
    // Vector operations
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }
    
    Vector2 normalized() const {
        float len = length();
        return (len > 0.0f) ? (*this / len) : Vector2::Zero;
    }
    
    void normalize() {
        float len = length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
        }
    }
    
    float dot(const Vector2& other) const { return x * other.x + y * other.y; }
    
    float distance(const Vector2& other) const { return (*this - other).length(); }
    float distanceSquared(const Vector2& other) const { return (*this - other).lengthSquared(); }
    
    // Static utility functions
    static float Dot(const Vector2& a, const Vector2& b) { return a.dot(b); }
    static float Distance(const Vector2& a, const Vector2& b) { return a.distance(b); }
    static Vector2 Lerp(const Vector2& a, const Vector2& b, float t) { return a + (b - a) * t; }
    static Vector2 Min(const Vector2& a, const Vector2& b) { return Vector2(std::min(a.x, b.x), std::min(a.y, b.y)); }
    static Vector2 Max(const Vector2& a, const Vector2& b) { return Vector2(std::max(a.x, b.x), std::max(a.y, b.y)); }
    
    // Stream output
    friend std::ostream& operator<<(std::ostream& os, const Vector2& v) {
        os << "Vector2(" << v.x << ", " << v.y << ")";
        return os;
    }
};

// Static member definitions
inline const Vector2 Vector2::Zero = Vector2(0.0f, 0.0f);
inline const Vector2 Vector2::One = Vector2(1.0f, 1.0f);
inline const Vector2 Vector2::UnitX = Vector2(1.0f, 0.0f);
inline const Vector2 Vector2::UnitY = Vector2(0.0f, 1.0f);

// Scalar multiplication (scalar * vector)
inline Vector2 operator*(float scalar, const Vector2& v) { return v * scalar; }

} // namespace Math
} // namespace Crescent
