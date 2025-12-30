#pragma once

#include <memory>
#include <string>
#include "GizmoSystem.hpp"

namespace Crescent {

// Forward declarations
class Renderer;
class SelectionSystem;
class GizmoSystem;

class Engine {
public:
    Engine();
    ~Engine();
    
    // Initialize the engine
    bool initialize();
    
    // Shutdown the engine
    void shutdown();
    
    // Update engine per frame
    void update(float deltaTime);
    
    // Render frame
    void render();
    
    // Input handling
    void handleKeyDown(unsigned short keyCode);
    void handleKeyUp(unsigned short keyCode);
    void handleMouseMove(float deltaX, float deltaY);
    void handleMouseButton(int button, bool pressed);
    
    // Mouse picking and gizmo interaction
    void handleMouseClick(float x, float y, float screenWidth, float screenHeight, bool additive);
    void handleMouseDrag(float x, float y, float screenWidth, float screenHeight);
    void handleMouseUp();
    
    // Gizmo controls
    void setGizmoMode(GizmoMode mode);
    void setGizmoSpace(GizmoSpace space);
    void toggleGizmoMode(); // Q key - cycle through translate/rotate/scale
    void toggleGizmoSpace(); // E key - toggle world/local
    GizmoSystem* getGizmoSystem() const { return m_gizmoSystem.get(); }
    
    // Get renderer instance
    Renderer* getRenderer() const { return m_renderer.get(); }
    
    // Get singleton instance
    static Engine& getInstance();
    
private:
    std::unique_ptr<Renderer> m_renderer;
    bool m_isInitialized;
    
    // Selection and Gizmo systems
    std::unique_ptr<SelectionSystem> m_selectionSystem;
    std::unique_ptr<GizmoSystem> m_gizmoSystem;
    
    // Gizmo state
    GizmoMode m_currentGizmoMode;
    GizmoSpace m_currentGizmoSpace;
    
    // Mouse state for gizmo interaction
    bool m_isLeftMouseDown;
    float m_lastMouseX;
    float m_lastMouseY;
    
    // Singleton instance
    static Engine* s_instance;
};

} // namespace Crescent
