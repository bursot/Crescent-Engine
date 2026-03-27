#import "RuntimeBridge.h"

#import "../../Bridge/CrescentEngineBridge.h"

@implementation RuntimeBridge

+ (instancetype)shared {
    static RuntimeBridge *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[RuntimeBridge alloc] init];
    });
    return sharedInstance;
}

- (CrescentEngineBridge *)bridge {
    return [CrescentEngineBridge shared];
}

- (BOOL)initialize { return [[self bridge] initialize]; }
- (void)shutdown { [[self bridge] shutdown]; }
- (BOOL)tick:(float)deltaTime { return [[self bridge] tick:deltaTime]; }
- (void)setSceneMetalLayer:(CAMetalLayer *)layer { [[self bridge] setSceneMetalLayer:layer]; }
- (void)setGameMetalLayer:(CAMetalLayer *)layer { [[self bridge] setGameMetalLayer:layer]; }
- (void)setPreviewMetalLayer:(CAMetalLayer *)layer { (void)layer; }
- (void)resizeSceneWithWidth:(float)width height:(float)height { [[self bridge] resizeSceneWithWidth:width height:height]; }
- (void)resizeGameWithWidth:(float)width height:(float)height { [[self bridge] resizeGameWithWidth:width height:height]; }
- (void)resizePreviewWithWidth:(float)width height:(float)height { (void)width; (void)height; }
- (void)setAnimationPreviewTargetUUID:(NSString *)uuid { (void)uuid; }
- (void)handleKeyDown:(unsigned short)keyCode { [[self bridge] handleKeyDown:keyCode]; }
- (void)handleKeyUp:(unsigned short)keyCode { [[self bridge] handleKeyUp:keyCode]; }
- (void)handleMouseMoveWithDeltaX:(float)deltaX deltaY:(float)deltaY { [[self bridge] handleMouseMoveWithDeltaX:deltaX deltaY:deltaY]; }
- (void)handleMouseButton:(int)button pressed:(BOOL)pressed { [[self bridge] handleMouseButton:button pressed:pressed]; }
- (BOOL)openProjectAtPath:(NSString *)path { return [[self bridge] openProjectAtPath:path]; }
- (NSDictionary *)getProjectSettings { return [[self bridge] getProjectSettings]; }
- (NSDictionary *)getProjectInfo { return [[self bridge] getProjectInfo]; }
- (NSDictionary *)getSceneSettings { return [[self bridge] getSceneSettings]; }
- (void)setSceneSettings:(NSDictionary *)settings { [[self bridge] setSceneSettings:settings]; }
- (NSDictionary *)buildSceneStaticLightingLayout { return [[self bridge] buildSceneStaticLightingLayout]; }
- (NSDictionary *)bakeSceneStaticLightmaps { return [[self bridge] bakeSceneStaticLightmaps]; }
- (NSDictionary *)bakeSceneStaticLighting { return [[self bridge] bakeSceneStaticLighting]; }
- (BOOL)cookEnvironmentMapAtPath:(NSString *)path outputPath:(NSString *)outputPath { return [[self bridge] cookEnvironmentMap:path outputPath:outputPath]; }
- (BOOL)cookStaticLightmapAtPath:(NSString *)path outputPath:(NSString *)outputPath { return [[self bridge] cookStaticLightmap:path outputPath:outputPath]; }
- (BOOL)saveSceneAtPath:(NSString *)path { return [[self bridge] saveSceneAtPath:path]; }
- (BOOL)saveCookedRuntimeSceneAtPath:(NSString *)path includeEditorOnly:(BOOL)includeEditorOnly {
    return [[self bridge] saveCookedRuntimeSceneAtPath:path includeEditorOnly:includeEditorOnly];
}
- (BOOL)loadSceneAtPath:(NSString *)path { return [[self bridge] loadSceneAtPath:path]; }
- (void)setViewMode:(int)mode { [[self bridge] setViewMode:mode]; }
- (void)enterPlayMode { [[self bridge] enterPlayMode]; }
- (void)exitPlayMode { [[self bridge] exitPlayMode]; }

@end
