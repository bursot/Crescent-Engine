#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

@interface CrescentEngineBridge : NSObject

+ (instancetype)shared;

- (BOOL)initialize;
- (void)shutdown;
- (void)update:(float)deltaTime;
- (void)render;
- (void)setMetalLayer:(CAMetalLayer *)layer;
- (void)resizeWithWidth:(float)width height:(float)height;

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

// Scene query
- (NSArray<NSDictionary *> *)getAllEntityInfo; // Returns array of {uuid, name}
- (NSString *)getSelectedEntityUUID NS_SWIFT_NAME(getSelectedEntityUUID());
- (NSArray<NSString *> *)getAllSelectedEntityUUIDs NS_SWIFT_NAME(getAllSelectedUUIDs()); // Returns ALL selected UUIDs
- (void)setSelectionByUUID:(NSArray<NSString *> *)uuids NS_SWIFT_NAME(setSelection(uuids:));
- (void)selectEntityByUUID:(NSString *)uuid NS_SWIFT_NAME(selectEntity(uuid:));
- (void)clearSelection;

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

// Material editing (by UUID)
- (NSDictionary *)getMaterialInfoForEntity:(NSString *)uuid;
- (void)setMaterialScalarForEntity:(NSString *)uuid property:(NSString *)property value:(float)value;
- (void)setMaterialColorForEntity:(NSString *)uuid property:(NSString *)property r:(float)r g:(float)g b:(float)b a:(float)a;
- (BOOL)loadTextureForEntity:(NSString *)uuid slot:(NSString *)slot path:(NSString *)path;
- (void)clearTextureForEntity:(NSString *)uuid slot:(NSString *)slot;

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
- (void)setAssetRootPath:(NSString *)path NS_SWIFT_NAME(setAssetRoot(path:));
- (BOOL)createProjectAtPath:(NSString *)path name:(NSString *)name NS_SWIFT_NAME(createProject(path:name:));
- (BOOL)openProjectAtPath:(NSString *)path NS_SWIFT_NAME(openProject(path:));
- (NSDictionary *)getProjectInfo NS_SWIFT_NAME(getProjectInfo());
- (NSString *)importAssetAtPath:(NSString *)path type:(NSString *)type NS_SWIFT_NAME(importAsset(path:type:));
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
- (BOOL)getPhysicsDebugDraw NS_SWIFT_NAME(getPhysicsDebugDraw());
- (void)setPhysicsDebugDraw:(BOOL)enabled NS_SWIFT_NAME(setPhysicsDebugDraw(enabled:));

// Debug toggles (editor-only use)
- (void)setDebugDrawShadowAtlas:(BOOL)enabled;
- (void)setDebugDrawCascades:(BOOL)enabled;
- (void)setDebugDrawPointFrusta:(BOOL)enabled;

@end

NS_ASSUME_NONNULL_END
