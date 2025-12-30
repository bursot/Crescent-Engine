#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <random>

namespace Crescent {

// Universally Unique Identifier for entities
class UUID {
public:
    UUID();
    UUID(uint64_t uuid);
    UUID(const UUID&) = default;
    
    operator uint64_t() const { return m_UUID; }
    
    bool isValid() const { return m_UUID != 0; }
    std::string toString() const;
    
    bool operator==(const UUID& other) const { return m_UUID == other.m_UUID; }
    bool operator!=(const UUID& other) const { return m_UUID != other.m_UUID; }
    bool operator<(const UUID& other) const { return m_UUID < other.m_UUID; }
    
    static UUID Invalid() { return UUID(0); }
    
private:
    uint64_t m_UUID;
};

} // namespace Crescent

// Hash function for UUID to use in unordered_map
namespace std {
    template <>
    struct hash<Crescent::UUID> {
        size_t operator()(const Crescent::UUID& uuid) const {
            return hash<uint64_t>()(static_cast<uint64_t>(uuid));
        }
    };
}
