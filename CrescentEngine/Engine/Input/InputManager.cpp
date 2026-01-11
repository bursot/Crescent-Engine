#include "InputManager.hpp"

namespace Crescent {

void InputManager::setKeyPressed(KeyCode key, bool pressed) {
    m_keyStates[key] = pressed;
}

bool InputManager::isKeyPressed(KeyCode key) const {
    auto it = m_keyStates.find(key);
    return it != m_keyStates.end() && it->second;
}

bool InputManager::isKeyDown(KeyCode key) const {
    bool currentlyPressed = isKeyPressed(key);
    auto it = m_keyStatesPrevious.find(key);
    bool previouslyPressed = it != m_keyStatesPrevious.end() && it->second;
    return currentlyPressed && !previouslyPressed;
}

bool InputManager::isKeyUp(KeyCode key) const {
    bool currentlyPressed = isKeyPressed(key);
    auto it = m_keyStatesPrevious.find(key);
    bool previouslyPressed = it != m_keyStatesPrevious.end() && it->second;
    return !currentlyPressed && previouslyPressed;
}

void InputManager::setMouseButtonPressed(MouseButton button, bool pressed) {
    m_mouseButtonStates[button] = pressed;
}

bool InputManager::isMouseButtonPressed(MouseButton button) const {
    auto it = m_mouseButtonStates.find(button);
    return it != m_mouseButtonStates.end() && it->second;
}

void InputManager::setMousePosition(const Math::Vector2& position) {
    m_mousePosition = position;
}

void InputManager::setMouseDelta(const Math::Vector2& delta) {
    m_mouseDelta = delta;
}

void InputManager::setMouseScrollDelta(float delta) {
    m_mouseScrollDelta = delta;
}

void InputManager::update() {
    m_keyStatesPrevious = m_keyStates;
    m_mouseButtonStatesPrevious = m_mouseButtonStates;
    m_mouseDelta = Math::Vector2(0.0f, 0.0f);
    m_mouseScrollDelta = 0.0f;
}

void InputManager::clear() {
    m_keyStates.clear();
    m_keyStatesPrevious.clear();
    m_mouseButtonStates.clear();
    m_mouseButtonStatesPrevious.clear();
    m_mousePosition = Math::Vector2(0.0f, 0.0f);
    m_mouseDelta = Math::Vector2(0.0f, 0.0f);
    m_mouseScrollDelta = 0.0f;
}

KeyCode InputManager::stringToKeyCode(const std::string& keyString) {
    if (keyString == "w" || keyString == "W") return KeyCode::W;
    if (keyString == "a" || keyString == "A") return KeyCode::A;
    if (keyString == "s" || keyString == "S") return KeyCode::S;
    if (keyString == "d" || keyString == "D") return KeyCode::D;
    if (keyString == "q" || keyString == "Q") return KeyCode::Q;
    if (keyString == "e" || keyString == "E") return KeyCode::E;
    if (keyString == "r" || keyString == "R") return KeyCode::R;
    if (keyString == " " || keyString == "Space" || keyString == "space") return KeyCode::Space;
    if (keyString == "Shift" || keyString == "shift") return KeyCode::Shift;
    if (keyString == "Control" || keyString == "control" || keyString == "Ctrl" || keyString == "ctrl") return KeyCode::Control;
    if (keyString == "Alt" || keyString == "alt") return KeyCode::Alt;
    if (keyString == "Command" || keyString == "command" || keyString == "Cmd" || keyString == "cmd") return KeyCode::Command;
    if (keyString == "Escape" || keyString == "escape" || keyString == "Esc" || keyString == "esc") return KeyCode::Escape;
    if (keyString == "Up" || keyString == "up") return KeyCode::Up;
    if (keyString == "Down" || keyString == "down") return KeyCode::Down;
    if (keyString == "Left" || keyString == "left") return KeyCode::Left;
    if (keyString == "Right" || keyString == "right") return KeyCode::Right;
    return KeyCode::Unknown;
}

} // namespace Crescent
