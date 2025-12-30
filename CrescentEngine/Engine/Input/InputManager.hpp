#pragma once

#include "../Math/Vector2.hpp"
#include <unordered_map>
#include <string>

namespace Crescent {

enum class KeyCode {
    W, A, S, D, Q, E,
    Shift, Control, Alt, Command,
    Space, Escape,
    Up, Down, Left, Right,
    Unknown
};

enum class MouseButton {
    Left, Right, Middle
};

class InputManager {
public:
    static InputManager& getInstance() {
        static InputManager instance;
        return instance;
    }
    
    void setKeyPressed(KeyCode key, bool pressed);
    bool isKeyPressed(KeyCode key) const;
    bool isKeyDown(KeyCode key) const;
    bool isKeyUp(KeyCode key) const;
    
    void setMouseButtonPressed(MouseButton button, bool pressed);
    bool isMouseButtonPressed(MouseButton button) const;
    
    void setMousePosition(const Math::Vector2& position);
    Math::Vector2 getMousePosition() const { return m_mousePosition; }
    
    void setMouseDelta(const Math::Vector2& delta);
    Math::Vector2 getMouseDelta() const { return m_mouseDelta; }
    
    void setMouseScrollDelta(float delta);
    float getMouseScrollDelta() const { return m_mouseScrollDelta; }
    
    void update();
    void clear();
    
    static KeyCode stringToKeyCode(const std::string& keyString);
    
private:
    InputManager() = default;
    ~InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    
    std::unordered_map<KeyCode, bool> m_keyStates;
    std::unordered_map<KeyCode, bool> m_keyStatesPrevious;
    std::unordered_map<MouseButton, bool> m_mouseButtonStates;
    std::unordered_map<MouseButton, bool> m_mouseButtonStatesPrevious;
    
    Math::Vector2 m_mousePosition{0.0f, 0.0f};
    Math::Vector2 m_mouseDelta{0.0f, 0.0f};
    float m_mouseScrollDelta = 0.0f;
};

} // namespace Crescent
