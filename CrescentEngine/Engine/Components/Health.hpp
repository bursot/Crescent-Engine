#pragma once

#include "../ECS/Component.hpp"
#include "../ECS/Entity.hpp"
#include "../Scene/Scene.hpp"
#include <algorithm>

namespace Crescent {

class Health : public Component {
public:
    COMPONENT_TYPE(Health)

    Health() = default;
    virtual ~Health() = default;

    float getMaxHealth() const { return m_MaxHealth; }
    void setMaxHealth(float value) {
        m_MaxHealth = std::max(0.0f, value);
        if (m_CurrentHealth > m_MaxHealth) {
            m_CurrentHealth = m_MaxHealth;
        }
    }

    float getCurrentHealth() const { return m_CurrentHealth; }
    void setCurrentHealth(float value) {
        m_CurrentHealth = std::max(0.0f, std::min(value, m_MaxHealth));
        if (m_CurrentHealth <= 0.0f && hasStarted()) {
            handleDeath();
        }
    }

    bool getDestroyOnDeath() const { return m_DestroyOnDeath; }
    void setDestroyOnDeath(bool value) { m_DestroyOnDeath = value; }

    bool isDead() const { return m_IsDead; }

    void reset() {
        m_CurrentHealth = m_MaxHealth;
        m_IsDead = false;
    }

    void heal(float amount) {
        if (amount <= 0.0f || m_IsDead) {
            return;
        }
        m_CurrentHealth = std::min(m_MaxHealth, m_CurrentHealth + amount);
    }

    bool applyDamage(float amount) {
        if (amount <= 0.0f || m_IsDead) {
            return false;
        }
        m_CurrentHealth = std::max(0.0f, m_CurrentHealth - amount);
        if (m_CurrentHealth <= 0.0f && hasStarted()) {
            handleDeath();
            return true;
        }
        return false;
    }

    void OnCreate() override {
        if (m_MaxHealth < 0.0f) {
            m_MaxHealth = 0.0f;
        }
        if (m_CurrentHealth <= 0.0f || m_CurrentHealth > m_MaxHealth) {
            m_CurrentHealth = m_MaxHealth;
        }
    }

private:
    void handleDeath() {
        if (m_IsDead) {
            return;
        }
        m_IsDead = true;
        if (!m_DestroyOnDeath) {
            return;
        }
        Entity* entity = getEntity();
        Scene* scene = entity ? entity->getScene() : nullptr;
        if (scene && entity) {
            scene->destroyEntity(entity);
        }
    }

    float m_MaxHealth = 100.0f;
    float m_CurrentHealth = 100.0f;
    bool m_DestroyOnDeath = true;
    bool m_IsDead = false;
};

} // namespace Crescent
