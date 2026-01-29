#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

@interface CrescentEngineBridge : NSObject

+ (instancetype)shared;

- (BOOL)initialize;
- (void)shutdown;
- (void)update:(float)deltaTime;
- (void)render;
- (BOOL)tick:(float)deltaTime NS_SWIFT_NAME(tick(deltaTime:));
- (void)setMetalLayer:(CAMetalLayer *)layer;
- (void)resizeWithWidth:(float)width height:(float)height;
- (void)setSceneMetalLayer:(CAMetalLayer *)layer;
- (void)setGameMetalLayer:(CAMetalLayer *)layer;
- (void)resizeSceneWithWidth:(float)width height:(float)height;
- (void)resizeGameWithWidth:(float)width height:(float)height;

// Input handling
- (void)handleKeyDown:(unsigned short)keyCode;
- (void)handleKeyUp:(unsigned short)keyCode;
- (void)handleMouseMoveWithDeltaX:(float)deltaX deltaY:(float)deltaY;
- (void)handleMouseButton:(int)button pressed:(BOOL)pressed;

// Scene editing commands
- (void)createCube;
- (void)createSphere;
- (void)createPlane;
- (void)createCylinder;
- (void)createCone;
- (void)createTorus;
- (void)createCapsule;
- (void)createDirectionalLight;
- (void)createPointLight;
- (void)createSpotLight;
- (void)createDecal;
- (void)createCamera;
- (void)createEmpty;
- (BOOL)importModelAtPath:(NSString *)path NS_SWIFT_NAME(importModel(path:));
- (BOOL)importModelAtPath:(NSString *)path options:(NSDictionary *)options NS_SWIFT_NAME(importModel(path:options:));
- (void)deleteEntitiesByUUID:(NSArray<NSString *> *)uuids NS_SWIFT_NAME(deleteEntities(uuids:));
- (NSArray<NSString *> *)duplicateEntitiesByUUID:(NSArray<NSString *> *)uuids NS_SWIFT_NAME(duplicateEntities(uuids:));

// Scene query
- (NSArray<NSDictionary *> *)getAllEntityInfo; // Returns array of {uuid, name, parent}
- (NSString *)getSelectedEntityUUID NS_SWIFT_NAME(getSelectedEntityUUID());
- (NSArray<NSString *> *)getAllSelectedEntityUUIDs NS_SWIFT_NAME(getAllSelectedUUIDs()); // Returns ALL selected UUIDs
- (void)setSelectionByUUID:(NSArray<NSString *> *)uuids NS_SWIFT_NAME(setSelection(uuids:));
- (void)selectEntityByUUID:(NSString *)uuid NS_SWIFT_NAME(selectEntity(uuid:));
- (void)clearSelection;
- (BOOL)setEntityParent:(NSString *)childUUID parent:(NSString *)parentUUID NS_SWIFT_NAME(setEntityParent(child:parent:));
- (BOOL)setEntityName:(NSString *)uuid name:(NSString *)name NS_SWIFT_NAME(setEntityName(uuid:name:));
- (NSString *)buildHLODFromSelection:(NSArray<NSString *> *)uuids NS_SWIFT_NAME(buildHLOD(from:));

// Entity transform query (by UUID)
- (NSArray<NSNumber *> *)getEntityPositionByUUID:(NSString *)uuid NS_SWIFT_NAME(getPosition(uuid:));
- (NSArray<NSNumber *> *)getEntityRotationByUUID:(NSString *)uuid NS_SWIFT_NAME(getRotation(uuid:));
- (NSArray<NSNumber *> *)getEntityScaleByUUID:(NSString *)uuid NS_SWIFT_NAME(getScale(uuid:));
- (void)setEntityPositionByUUID:(NSString *)uuid x:(float)x y:(float)y z:(float)z NS_SWIFT_NAME(setPosition(uuid:x:y:z:));
- (void)setEntityRotationByUUID:(NSString *)uuid x:(float)x y:(float)y z:(float)z NS_SWIFT_NAME(setRotation(uuid:x:y:z:));
- (void)setEntityScaleByUUID:(NSString *)uuid x:(float)x y:(float)y z:(float)z NS_SWIFT_NAME(setScale(uuid:x:y:z:));

// Mouse picking and gizmo interaction
- (void)handleMouseClickAtX:(float)x y:(float)y screenWidth:(float)width screenHeight:(float)height additive:(BOOL)additive;
- (void)handleMouseDragAtX:(float)x y:(float)y screenWidth:(float)width screenHeight:(float)height;
- (void)handleMouseUpEvent;

// Gizmo controls
- (void)setGizmoMode:(int)mode; // 0=Translate, 1=Rotate, 2=Scale
- (void)toggleGizmoMode;
- (void)toggleGizmoSpace;

// Camera controls
- (float)getCameraMoveSpeed;
- (void)setCameraMoveSpeed:(float)speed;
- (NSDictionary *)getSceneCameraBasis NS_SWIFT_NAME(getSceneCameraBasis());

// Material editing (by UUID)
- (NSDictionary *)getMaterialInfoForEntity:(NSString *)uuid;
- (NSDictionary *)getFoliageAutoLodForEntity:(NSString *)uuid NS_SWIFT_NAME(getFoliageAutoLod(forEntity:));
- (void)setMaterialScalarForEntity:(NSString *)uuid property:(NSString *)property value:(float)value;
- (void)setMaterialColorForEntity:(NSString *)uuid property:(NSString *)property r:(float)r g:(float)g b:(float)b a:(float)a;
- (BOOL)loadTextureForEntity:(NSString *)uuid slot:(NSString *)slot path:(NSString *)path;
- (void)clearTextureForEntity:(NSString *)uuid slot:(NSString *)slot;
- (void)setMaterialScalarForEntityAllMaterials:(NSString *)uuid property:(NSString *)property value:(float)value NS_SWIFT_NAME(setMaterialScalarForEntityAllMaterials(_:property:value:));
- (void)setMaterialColorForEntityAllMaterials:(NSString *)uuid property:(NSString *)property r:(float)r g:(float)g b:(float)b a:(float)a NS_SWIFT_NAME(setMaterialColorForEntityAllMaterials(_:property:r:g:b:a:));
- (BOOL)loadTextureForEntityAllMaterials:(NSString *)uuid slot:(NSString *)slot path:(NSString *)path NS_SWIFT_NAME(loadTextureForEntityAllMaterials(_:slot:path:));
- (void)clearTextureForEntityAllMaterials:(NSString *)uuid slot:(NSString *)slot NS_SWIFT_NAME(clearTextureForEntityAllMaterials(_:slot:));
- (BOOL)bakeImpostorAtlasForEntity:(NSString *)uuid rows:(NSInteger)rows cols:(NSInteger)cols tileSize:(NSInteger)tileSize NS_SWIFT_NAME(bakeImpostorAtlas(forEntity:rows:cols:tileSize:));

// Render stats
- (NSDictionary *)getRenderStats NS_SWIFT_NAME(getRenderStats());

// Environment / IBL controls
- (NSDictionary *)getEnvironmentSettings;
- (BOOL)loadEnvironmentMap:(NSString *)path;
- (void)resetEnvironment;
- (void)setEnvironmentExposure:(float)ev;
- (void)setEnvironmentIBLIntensity:(float)intensity;
- (void)setEnvironmentSkyIntensity:(float)intensity;
- (void)setEnvironmentRotationWithX:(float)x y:(float)y z:(float)z;
- (void)setEnvironmentTintWithR:(float)r g:(float)g b:(float)b;
- (void)setEnvironmentSaturation:(float)saturation;
- (void)setEnvironmentContrast:(float)contrast;
- (void)setEnvironmentBlur:(float)blur;
- (void)setEnvironmentSkyboxVisible:(BOOL)visible;

// Lights
- (NSArray<NSDictionary *> *)listLights;
- (NSDictionary *)getLightInfo:(NSString *)uuid;
- (BOOL)setLightInfo:(NSString *)uuid info:(NSDictionary *)info;

// Decals
- (NSDictionary *)getDecalInfo:(NSString *)uuid;
- (BOOL)setDecalInfo:(NSString *)uuid info:(NSDictionary *)info;
- (BOOL)loadDecalTexture:(NSString *)uuid slot:(NSString *)slot path:(NSString *)path;
- (void)clearDecalTexture:(NSString *)uuid slot:(NSString *)slot;

// Scene IO / Play mode
- (BOOL)saveSceneAtPath:(NSString *)path NS_SWIFT_NAME(saveScene(path:));
- (BOOL)loadSceneAtPath:(NSString *)path NS_SWIFT_NAME(loadScene(path:));
- (void)enterPlayMode NS_SWIFT_NAME(enterPlayMode());
- (void)exitPlayMode NS_SWIFT_NAME(exitPlayMode());
- (BOOL)isPlaying NS_SWIFT_NAME(isPlaying());
- (BOOL)isPaused NS_SWIFT_NAME(isPaused());
- (void)setPaused:(BOOL)paused NS_SWIFT_NAME(setPaused(_:));
- (float)getTimeScale NS_SWIFT_NAME(getTimeScale());
- (void)setTimeScale:(float)scale NS_SWIFT_NAME(setTimeScale(_:));
- (void)setViewMode:(int)mode NS_SWIFT_NAME(setViewMode(_:));
- (int)getViewMode NS_SWIFT_NAME(getViewMode());
- (void)setAssetRootPath:(NSString *)path NS_SWIFT_NAME(setAssetRoot(path:));
- (BOOL)createProjectAtPath:(NSString *)path name:(NSString *)name NS_SWIFT_NAME(createProject(path:name:));
- (BOOL)openProjectAtPath:(NSString *)path NS_SWIFT_NAME(openProject(path:));
- (NSDictionary *)getProjectInfo NS_SWIFT_NAME(getProjectInfo());
- (NSString *)importAssetAtPath:(NSString *)path type:(NSString *)type NS_SWIFT_NAME(importAsset(path:type:));
- (BOOL)moveAssetAtPath:(NSString *)sourcePath toPath:(NSString *)targetPath overwrite:(BOOL)overwrite NS_SWIFT_NAME(moveAsset(source:to:overwrite:));
- (NSDictionary *)getAssetMetaAtPath:(NSString *)path NS_SWIFT_NAME(getAssetMeta(path:));
- (BOOL)updateModelImportSettings:(NSString *)guid settings:(NSDictionary *)settings NS_SWIFT_NAME(updateModelImportSettings(guid:settings:));
- (BOOL)updateTextureImportSettings:(NSString *)guid settings:(NSDictionary *)settings NS_SWIFT_NAME(updateTextureImportSettings(guid:settings:));
- (BOOL)updateHdriImportSettings:(NSString *)guid settings:(NSDictionary *)settings NS_SWIFT_NAME(updateHdriImportSettings(guid:settings:));
- (BOOL)reimportModelAsset:(NSString *)guid NS_SWIFT_NAME(reimportModelAsset(guid:));
- (BOOL)reimportTextureAsset:(NSString *)guid NS_SWIFT_NAME(reimportTextureAsset(guid:));
- (BOOL)reimportHdriAsset:(NSString *)guid NS_SWIFT_NAME(reimportHdriAsset(guid:));
- (NSDictionary *)getProjectSettings NS_SWIFT_NAME(getProjectSettings());
- (void)setProjectSettings:(NSDictionary *)settings NS_SWIFT_NAME(setProjectSettings(settings:));
- (NSDictionary *)getSceneSettings NS_SWIFT_NAME(getSceneSettings());
- (void)setSceneSettings:(NSDictionary *)settings NS_SWIFT_NAME(setSceneSettings(settings:));

// Animation / Skinned Mesh
- (NSDictionary *)getSkinnedMeshInfo:(NSString *)uuid NS_SWIFT_NAME(getSkinnedMeshInfo(uuid:));
- (BOOL)setSkinnedMeshInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setSkinnedMeshInfo(uuid:info:));
- (BOOL)setAnimatorParameter:(NSString *)uuid name:(NSString *)name type:(NSString *)type value:(id)value NS_SWIFT_NAME(setAnimatorParameter(uuid:name:type:value:));
- (BOOL)fireAnimatorTrigger:(NSString *)uuid name:(NSString *)name NS_SWIFT_NAME(fireAnimatorTrigger(uuid:name:));
- (BOOL)setAnimatorRootMotion:(NSString *)uuid enabled:(BOOL)enabled applyPosition:(BOOL)applyPosition applyRotation:(BOOL)applyRotation NS_SWIFT_NAME(setAnimatorRootMotion(uuid:enabled:applyPosition:applyRotation:));
- (NSDictionary *)getAnimatorGraph:(NSString *)uuid NS_SWIFT_NAME(getAnimatorGraph(uuid:));
- (BOOL)setAnimatorGraph:(NSString *)uuid graph:(NSDictionary *)graph NS_SWIFT_NAME(setAnimatorGraph(uuid:graph:));
- (NSArray<NSDictionary *> *)getAnimationEvents:(NSString *)uuid clipIndex:(NSInteger)clipIndex NS_SWIFT_NAME(getAnimationEvents(uuid:clipIndex:));
- (BOOL)setAnimationEvents:(NSString *)uuid clipIndex:(NSInteger)clipIndex events:(NSArray<NSDictionary *> *)events NS_SWIFT_NAME(setAnimationEvents(uuid:clipIndex:events:));
- (NSArray<NSDictionary *> *)pollAnimatorEvents:(NSString *)uuid NS_SWIFT_NAME(pollAnimatorEvents(uuid:));
- (NSArray<NSDictionary *> *)getAnimationClipsInfo:(NSString *)uuid NS_SWIFT_NAME(getAnimationClipsInfo(uuid:));
- (NSDictionary *)getSkeletonInfo:(NSString *)uuid NS_SWIFT_NAME(getSkeletonInfo(uuid:));
- (NSDictionary *)getIKConstraintInfo:(NSString *)uuid NS_SWIFT_NAME(getIKConstraintInfo(uuid:));
- (BOOL)setIKConstraintInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setIKConstraintInfo(uuid:info:));

// Physics
- (NSDictionary *)getRigidbodyInfo:(NSString *)uuid NS_SWIFT_NAME(getRigidbodyInfo(uuid:));
- (BOOL)setRigidbodyInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setRigidbodyInfo(uuid:info:));
- (BOOL)addRigidbody:(NSString *)uuid NS_SWIFT_NAME(addRigidbody(uuid:));
- (void)removeRigidbody:(NSString *)uuid NS_SWIFT_NAME(removeRigidbody(uuid:));
- (NSDictionary *)getColliderInfo:(NSString *)uuid NS_SWIFT_NAME(getColliderInfo(uuid:));
- (BOOL)setColliderInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setColliderInfo(uuid:info:));
- (BOOL)addCollider:(NSString *)uuid NS_SWIFT_NAME(addCollider(uuid:));
- (void)removeCollider:(NSString *)uuid NS_SWIFT_NAME(removeCollider(uuid:));
- (NSDictionary *)getHealthInfo:(NSString *)uuid NS_SWIFT_NAME(getHealthInfo(uuid:));
- (BOOL)setHealthInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setHealthInfo(uuid:info:));
- (BOOL)addHealth:(NSString *)uuid NS_SWIFT_NAME(addHealth(uuid:));
- (void)removeHealth:(NSString *)uuid NS_SWIFT_NAME(removeHealth(uuid:));
- (NSDictionary *)getCharacterControllerInfo:(NSString *)uuid NS_SWIFT_NAME(getCharacterControllerInfo(uuid:));
- (BOOL)setCharacterControllerInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setCharacterControllerInfo(uuid:info:));
- (BOOL)addCharacterController:(NSString *)uuid NS_SWIFT_NAME(addCharacterController(uuid:));
- (void)removeCharacterController:(NSString *)uuid NS_SWIFT_NAME(removeCharacterController(uuid:));
- (NSDictionary *)getFirstPersonControllerInfo:(NSString *)uuid NS_SWIFT_NAME(getFirstPersonControllerInfo(uuid:));
- (BOOL)setFirstPersonControllerInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setFirstPersonControllerInfo(uuid:info:));
- (BOOL)addFirstPersonController:(NSString *)uuid NS_SWIFT_NAME(addFirstPersonController(uuid:));
- (void)removeFirstPersonController:(NSString *)uuid NS_SWIFT_NAME(removeFirstPersonController(uuid:));
- (NSNumber *)getFireEventCounter NS_SWIFT_NAME(getFireEventCounter());
- (NSDictionary *)getAudioSourceInfo:(NSString *)uuid NS_SWIFT_NAME(getAudioSourceInfo(uuid:));
- (BOOL)setAudioSourceInfo:(NSString *)uuid info:(NSDictionary *)info NS_SWIFT_NAME(setAudioSourceInfo(uuid:info:));
- (BOOL)addAudioSource:(NSString *)uuid NS_SWIFT_NAME(addAudioSource(uuid:));
- (void)removeAudioSource:(NSString *)uuid NS_SWIFT_NAME(removeAudioSource(uuid:));
- (BOOL)getPhysicsDebugDraw NS_SWIFT_NAME(getPhysicsDebugDraw());
- (void)setPhysicsDebugDraw:(BOOL)enabled NS_SWIFT_NAME(setPhysicsDebugDraw(enabled:));

// Debug toggles (editor-only use)
- (void)setDebugDrawShadowAtlas:(BOOL)enabled;
- (void)setDebugDrawCascades:(BOOL)enabled;
- (void)setDebugDrawPointFrusta:(BOOL)enabled;

@end

NS_ASSUME_NONNULL_END
