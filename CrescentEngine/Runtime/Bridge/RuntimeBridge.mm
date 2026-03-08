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
- (void)resizeSceneWithWidth:(float)width height:(float)height { [[self bridge] resizeSceneWithWidth:width height:height]; }
- (void)resizeGameWithWidth:(float)width height:(float)height { [[self bridge] resizeGameWithWidth:width height:height]; }
- (void)handleKeyDown:(unsigned short)keyCode { [[self bridge] handleKeyDown:keyCode]; }
- (void)handleKeyUp:(unsigned short)keyCode { [[self bridge] handleKeyUp:keyCode]; }
- (void)handleMouseMoveWithDeltaX:(float)deltaX deltaY:(float)deltaY { [[self bridge] handleMouseMoveWithDeltaX:deltaX deltaY:deltaY]; }
- (void)handleMouseButton:(int)button pressed:(BOOL)pressed { [[self bridge] handleMouseButton:button pressed:pressed]; }
- (BOOL)openProjectAtPath:(NSString *)path { return [[self bridge] openProjectAtPath:path]; }
- (NSDictionary *)getProjectSettings { return [[self bridge] getProjectSettings]; }
- (NSDictionary *)getProjectInfo { return [[self bridge] getProjectInfo]; }
- (BOOL)loadSceneAtPath:(NSString *)path { return [[self bridge] loadSceneAtPath:path]; }
- (void)setViewMode:(int)mode { [[self bridge] setViewMode:mode]; }
- (void)enterPlayMode { [[self bridge] enterPlayMode]; }
- (void)exitPlayMode { [[self bridge] exitPlayMode]; }

@end
