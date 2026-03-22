#pragma once

#include "Scene.hpp"
#include "../Assets/AssetImportSettings.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Material.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/Light.hpp"
#include "../Components/Camera.hpp"
#include "../Components/Decal.hpp"
#include "../Animation/Skeleton.hpp"
#include "../Animation/AnimationClip.hpp"
#include <memory>
#include <vector>

namespace Crescent {

// Scene editing commands - used by editor
class SceneCommands {
public:
    using ModelImportOptions = ModelImportSettings;

    struct StaticLightingLayoutStats {
        int staticGeometryRendererCount = 0;
        int rendererCount = 0;
        int atlasCount = 0;
        int generatedUVRendererCount = 0;
        int reusedUVRendererCount = 0;
        int skippedRendererCount = 0;
    };

    struct StaticLightmapBakeStats {
        int atlasCount = 0;
        int bakedRendererCount = 0;
        int bakedLightCount = 0;
        int bakedTexelCount = 0;
        int staticGeometryRendererCount = 0;
        int layoutRendererCount = 0;
        int layoutSkippedRendererCount = 0;
        int generatedUVRendererCount = 0;
        int reusedUVRendererCount = 0;
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
    static std::vector<std::shared_ptr<AnimationClip>> importAnimationClipsForSkeleton(
        const std::string& path,
        const Skeleton& skeleton,
        const ModelImportOptions& options = ModelImportOptions());

    // Reimport assets by GUID (uses stored import settings)
    static bool reimportModelAsset(Scene* scene, const std::string& guid);
    static bool reimportTextureAsset(Scene* scene, const std::string& guid);
    static bool reimportHdriAsset(Scene* scene, const std::string& guid);
    
    // Destroy entities by UUID
    static void destroyEntitiesByUUID(Scene* scene, const std::vector<std::string>& uuids);
    
    // Get all entities in scene (for hierarchy view)
    static std::vector<Entity*> getAllEntities(Scene* scene);
    
    // Get entity by UUID
    static Entity* getEntityByUUID(Scene* scene, const std::string& uuidStr);

    // Parenting (for hierarchy view)
    static bool setParent(Scene* scene, const std::string& childUuid, const std::string& parentUuid);

    // HLOD (automatic bake)
    static Entity* buildHLOD(Scene* scene, const std::vector<std::string>& uuids, float lodStart = -1.0f, float lodEnd = -1.0f);

    // Prepares static meshes for UV lightmap baking: validates/generates UV1, estimates resolution,
    // assigns atlases, and writes per-renderer lightmap index + scale/offset metadata.
    static StaticLightingLayoutStats buildStaticLightingLayout(Scene* scene, const std::string& scenePath = "");

    // Bakes direct static lighting into atlas textures using UV1/lightmap layout.
    static StaticLightmapBakeStats bakeStaticLightmaps(Scene* scene, const std::string& scenePath = "");
};

} // namespace Crescent
