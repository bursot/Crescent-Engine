#pragma once

#include "Scene.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Material.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/Light.hpp"
#include "../Components/Camera.hpp"
#include "../Components/Decal.hpp"

namespace Crescent {

// Scene editing commands - used by editor
class SceneCommands {
public:
    struct ModelImportOptions {
        float scale;
        bool flipUVs;
        bool onlyLOD0;
        bool mergeStaticMeshes;

        ModelImportOptions()
            : scale(1.0f)
            , flipUVs(false)
            , onlyLOD0(false)
            , mergeStaticMeshes(false) {}
    };

    // Create primitive objects
    static Entity* createCube(Scene* scene, const std::string& name = "Cube");
    static Entity* createSphere(Scene* scene, const std::string& name = "Sphere");
    static Entity* createPlane(Scene* scene, const std::string& name = "Plane");
    static Entity* createCylinder(Scene* scene, const std::string& name = "Cylinder");
    static Entity* createCone(Scene* scene, const std::string& name = "Cone");
    static Entity* createTorus(Scene* scene, const std::string& name = "Torus");
    static Entity* createCapsule(Scene* scene, const std::string& name = "Capsule");
    
    // Create lights
    static Entity* createDirectionalLight(Scene* scene, const std::string& name = "Directional Light");
    static Entity* createPointLight(Scene* scene, const std::string& name = "Point Light");
    static Entity* createSpotLight(Scene* scene, const std::string& name = "Spot Light");
    static Entity* createDecal(Scene* scene, const std::string& name = "Decal");
    
    // Create camera
    static Entity* createCamera(Scene* scene, const std::string& name = "Camera");
    
    // Create empty entity
    static Entity* createEmpty(Scene* scene, const std::string& name = "GameObject");

    // Import a model file (static meshes + materials)
    static Entity* importModel(Scene* scene, const std::string& path, const ModelImportOptions& options = ModelImportOptions(), const std::string& name = "");
    
    // Destroy entities by UUID
    static void destroyEntitiesByUUID(Scene* scene, const std::vector<std::string>& uuids);
    
    // Get all entities in scene (for hierarchy view)
    static std::vector<Entity*> getAllEntities(Scene* scene);
    
    // Get entity by UUID
    static Entity* getEntityByUUID(Scene* scene, const std::string& uuidStr);
};

} // namespace Crescent
