#if EDITOR_APP
typealias AppBridgeType = CrescentEngineBridge
#elseif PLAYER_APP
typealias AppBridgeType = RuntimeBridge
#endif

enum AppBridge {
    static func shared() -> AppBridgeType {
        AppBridgeType.shared()
    }
}
