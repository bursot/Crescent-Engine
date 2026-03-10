#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

@interface RuntimeBridge : NSObject

+ (instancetype)shared;

- (BOOL)initialize;
- (void)shutdown;
- (BOOL)tick:(float)deltaTime NS_SWIFT_NAME(tick(deltaTime:));
- (void)setSceneMetalLayer:(CAMetalLayer *)layer;
- (void)setGameMetalLayer:(CAMetalLayer *)layer;
- (void)resizeSceneWithWidth:(float)width height:(float)height NS_SWIFT_NAME(resizeScene(withWidth:height:));
- (void)resizeGameWithWidth:(float)width height:(float)height NS_SWIFT_NAME(resizeGame(withWidth:height:));

- (void)handleKeyDown:(unsigned short)keyCode;
- (void)handleKeyUp:(unsigned short)keyCode;
- (void)handleMouseMoveWithDeltaX:(float)deltaX deltaY:(float)deltaY;
- (void)handleMouseButton:(int)button pressed:(BOOL)pressed;

- (BOOL)openProjectAtPath:(NSString *)path NS_SWIFT_NAME(openProject(path:));
- (NSDictionary *)getProjectSettings NS_SWIFT_NAME(getProjectSettings());
- (NSDictionary *)getProjectInfo NS_SWIFT_NAME(getProjectInfo());
- (NSDictionary *)bakeSceneVertexLighting NS_SWIFT_NAME(bakeSceneVertexLighting());
- (BOOL)cookEnvironmentMapAtPath:(NSString *)path outputPath:(NSString *)outputPath NS_SWIFT_NAME(cookEnvironmentMap(path:outputPath:));
- (BOOL)saveSceneAtPath:(NSString *)path NS_SWIFT_NAME(saveScene(path:));
- (BOOL)saveCookedRuntimeSceneAtPath:(NSString *)path includeEditorOnly:(BOOL)includeEditorOnly NS_SWIFT_NAME(saveCookedRuntimeScene(path:includeEditorOnly:));
- (BOOL)loadSceneAtPath:(NSString *)path NS_SWIFT_NAME(loadScene(path:));
- (void)setViewMode:(int)mode NS_SWIFT_NAME(setViewMode(_:));
- (void)enterPlayMode NS_SWIFT_NAME(enterPlayMode());
- (void)exitPlayMode NS_SWIFT_NAME(exitPlayMode());

@end

NS_ASSUME_NONNULL_END
