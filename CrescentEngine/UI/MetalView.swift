import SwiftUI
import MetalKit
import QuartzCore

enum RenderViewKind {
    case scene
    case game
}

// Protocol for input
protocol InputDelegate: AnyObject {
    func handleKeyDown(_ keyCode: UInt16)
    func handleKeyUp(_ keyCode: UInt16)
    func handleMouseMove(deltaX: Float, deltaY: Float)
    func handleMouseButton(_ button: Int, pressed: Bool)
}

struct MetalView: NSViewRepresentable {
    typealias NSViewType = MetalDisplayView

    let viewKind: RenderViewKind
    let isActive: Bool
    let drivesLoop: Bool
    let terrainPaintConfig: TerrainPaintConfig

    init(viewKind: RenderViewKind,
         isActive: Bool,
         drivesLoop: Bool = false,
         terrainPaintConfig: TerrainPaintConfig = TerrainPaintConfig()) {
        self.viewKind = viewKind
        self.isActive = isActive
        self.drivesLoop = drivesLoop
        self.terrainPaintConfig = terrainPaintConfig
    }
    
    func makeCoordinator() -> Coordinator {
        Coordinator(viewKind: viewKind, drivesLoop: drivesLoop)
    }
    
    func makeNSView(context: Context) -> MetalDisplayView {
        let metalView = MetalDisplayView()
        metalView.coordinator = context.coordinator
        metalView.allowsPicking = (viewKind == .scene) && isActive
        metalView.allowsCameraControl = isActive
        metalView.inputDelegate = isActive ? context.coordinator : nil
        metalView.terrainPaintEnabled = terrainPaintConfig.enabled
        metalView.terrainPaintTargetUUID = terrainPaintConfig.targetEntityUUID
        metalView.terrainPaintLayer = terrainPaintConfig.layer
        metalView.terrainBrushRadius = terrainPaintConfig.radius
        metalView.terrainBrushHardness = terrainPaintConfig.hardness
        metalView.terrainBrushStrength = terrainPaintConfig.strength
        metalView.terrainBrushSpacing = terrainPaintConfig.spacing
        metalView.terrainBrushMaskPreset = terrainPaintConfig.maskPreset
        metalView.terrainBrushMaskPath = terrainPaintConfig.maskPath
        metalView.terrainBrushAutoNormalize = terrainPaintConfig.autoNormalize
        context.coordinator.metalView = metalView
        
        // Initialize engine (delayed until view is laid out)
        DispatchQueue.main.async {
            context.coordinator.initializeEngine()
        }
        
        return metalView
    }
    
    func updateNSView(_ nsView: MetalDisplayView, context: Context) {
        context.coordinator.applyMetalLayerIfNeeded()
        nsView.allowsPicking = (viewKind == .scene) && isActive
        nsView.allowsCameraControl = isActive
        nsView.inputDelegate = isActive ? context.coordinator : nil
        nsView.terrainPaintEnabled = terrainPaintConfig.enabled
        nsView.terrainPaintTargetUUID = terrainPaintConfig.targetEntityUUID
        nsView.terrainPaintLayer = terrainPaintConfig.layer
        nsView.terrainBrushRadius = terrainPaintConfig.radius
        nsView.terrainBrushHardness = terrainPaintConfig.hardness
        nsView.terrainBrushStrength = terrainPaintConfig.strength
        nsView.terrainBrushSpacing = terrainPaintConfig.spacing
        nsView.terrainBrushMaskPreset = terrainPaintConfig.maskPreset
        nsView.terrainBrushMaskPath = terrainPaintConfig.maskPath
        nsView.terrainBrushAutoNormalize = terrainPaintConfig.autoNormalize
        context.coordinator.setInputMonitoring(active: isActive)
        if isActive, let window = nsView.window, window.firstResponder !== nsView {
            window.makeFirstResponder(nsView)
        }
    }
    
    class Coordinator: NSObject, InputDelegate {
        static var engineInitialized = false

        var bridge: CrescentEngineBridge?
        var metalView: MetalDisplayView?
        private var displayLink: Any? // Can be CVDisplayLink or CADisplayLink
        private weak var lastAppliedMetalLayer: CAMetalLayer?
        private var lastDrawableWidth: Float = 0
        private var lastDrawableHeight: Float = 0
        private var pendingResizeWorkItem: DispatchWorkItem?
        private var lastTime: CFTimeInterval = 0
        private var isEngineInitialized = false
        private let viewKind: RenderViewKind
        private let drivesLoop: Bool
        private var keyDownMonitor: Any?
        private var keyUpMonitor: Any?
        private var flagsChangedMonitor: Any?

        init(viewKind: RenderViewKind, drivesLoop: Bool) {
            self.viewKind = viewKind
            self.drivesLoop = drivesLoop
        }
        
        func initializeEngine() {
            guard !isEngineInitialized else { return }
            guard let metalView = metalView else { return }
            
            // Wait for layout
            guard metalView.bounds.size.width > 0 && metalView.bounds.size.height > 0 else {
                // Retry after a short delay
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                    self.initializeEngine()
                }
                return
            }
            
            // Initialize engine
            let bridge = CrescentEngineBridge.shared()
            if !Coordinator.engineInitialized {
                Coordinator.engineInitialized = bridge.initialize()
            }
            if Coordinator.engineInitialized {
                // Set metal layer
                if let metalLayer = metalView.metalLayer {
                    switch viewKind {
                    case .scene:
                        bridge.setSceneMetalLayer(metalLayer)
                    case .game:
                        bridge.setGameMetalLayer(metalLayer)
                    }
                    lastAppliedMetalLayer = metalLayer
                }
                
                self.bridge = bridge
                self.isEngineInitialized = Coordinator.engineInitialized

                handleResize(metalView.bounds.size)
                
                // Setup display link
                if drivesLoop {
                    setupDisplayLink()
                }
            }
        }
        
        func handleResize(_ size: CGSize) {
            pendingResizeWorkItem?.cancel()
            let workItem = DispatchWorkItem { [weak self] in
                self?.applyResize(size)
            }
            pendingResizeWorkItem = workItem

            let delay: DispatchTimeInterval = metalView?.window?.inLiveResize == true ? .milliseconds(33) : .milliseconds(12)
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: workItem)
        }

        private func applyResize(_ size: CGSize) {
            guard let metalView = metalView else { return }
            guard let bridge = bridge else { return }

            let scale = metalView.layer?.contentsScale ?? 2.0
            let width = Float(max(1.0, ceil(size.width * scale)))
            let height = Float(max(1.0, ceil(size.height * scale)))

            guard width > 0 && height > 0 else { return }
            if abs(width - lastDrawableWidth) < 2.0, abs(height - lastDrawableHeight) < 2.0 {
                return
            }
            lastDrawableWidth = width
            lastDrawableHeight = height
            switch viewKind {
            case .scene:
                bridge.resizeScene(withWidth: width, height: height)
            case .game:
                bridge.resizeGame(withWidth: width, height: height)
            }
        }

        func applyMetalLayerIfNeeded() {
            guard let bridge = bridge, let metalLayer = metalView?.metalLayer else { return }
            if lastAppliedMetalLayer === metalLayer {
                return
            }
            lastAppliedMetalLayer = metalLayer
            lastDrawableWidth = 0
            lastDrawableHeight = 0
            switch viewKind {
            case .scene:
                bridge.setSceneMetalLayer(metalLayer)
            case .game:
                bridge.setGameMetalLayer(metalLayer)
            }
        }
        
        func setupDisplayLink() {
            // Use modern API for macOS 15+ (suppresses deprecation warnings)
            if #available(macOS 15.0, *) {
                guard let metalView = metalView else { return }
                let link = metalView.displayLink(target: self, selector: #selector(renderFrameFromDisplayLink(_:)))
                link.add(to: .main, forMode: .common)
                self.displayLink = link
            } else {
                // Fallback for older macOS versions
                var cvDisplayLink: CVDisplayLink?
                CVDisplayLinkCreateWithActiveCGDisplays(&cvDisplayLink)
                
                guard let link = cvDisplayLink else { return }
                
                CVDisplayLinkSetOutputCallback(link, { (displayLink, inNow, inOutputTime, flagsIn, flagsOut, displayLinkContext) -> CVReturn in
                    let coordinator = Unmanaged<Coordinator>.fromOpaque(displayLinkContext!).takeUnretainedValue()
                    coordinator.renderFrame()
                    return kCVReturnSuccess
                }, Unmanaged.passUnretained(self).toOpaque())
                
                CVDisplayLinkStart(link)
                self.displayLink = link
            }
        }
        
        @objc func renderFrameFromDisplayLink(_ displayLink: CADisplayLink) {
            renderFrame()
        }
        
        func renderFrame() {
            guard drivesLoop, let bridge = bridge, isEngineInitialized else { return }
            
            let currentTime = CACurrentMediaTime()
            let deltaTime = lastTime == 0 ? 0.016 : Float(currentTime - lastTime)
            
            // Update and render
            if bridge.tick(deltaTime: deltaTime) {
                lastTime = currentTime
            }
        }
        
        deinit {
            pendingResizeWorkItem?.cancel()
            removeInputMonitors()
            if #available(macOS 15.0, *) {
                if let link = displayLink as? CADisplayLink {
                    link.invalidate()
                }
            } else {
                // CVDisplayLink is a CoreFoundation type (already optional pointer)
                if let link = displayLink {
                    CVDisplayLinkStop(link as! CVDisplayLink)
                }
            }
        }
        
        // MARK: - InputDelegate
        
        func handleKeyDown(_ keyCode: UInt16) {
            bridge?.handleKeyDown(keyCode)
        }
        
        func handleKeyUp(_ keyCode: UInt16) {
            bridge?.handleKeyUp(keyCode)
        }
        
        func handleMouseMove(deltaX: Float, deltaY: Float) {
            bridge?.handleMouseMove(withDeltaX: deltaX, deltaY: deltaY)
        }
        
        func handleMouseButton(_ button: Int, pressed: Bool) {
            bridge?.handleMouseButton(Int32(button), pressed: pressed)
        }
        
        func handleMouseClick(at point: CGPoint, viewSize: CGSize, additive: Bool) {
            guard let metalView = metalView else { return }
            let scale = metalView.layer?.contentsScale ?? 2.0
            let x = Float(point.x * scale)
            let y = Float(point.y * scale)
            let width = Float(viewSize.width * scale)
            let height = Float(viewSize.height * scale)
            
            bridge?.handleMouseClickAt(x: x, y: y, screenWidth: width, screenHeight: height, additive: additive)
        }
        
        func handleMouseDrag(at point: CGPoint, viewSize: CGSize) {
            guard let metalView = metalView else { return }
            let scale = metalView.layer?.contentsScale ?? 2.0
            let x = Float(point.x * scale)
            let y = Float(point.y * scale)
            let width = Float(viewSize.width * scale)
            let height = Float(viewSize.height * scale)
            
            bridge?.handleMouseDragAt(x: x, y: y, screenWidth: width, screenHeight: height)
        }
        
        func handleMouseUpEvent() {
            bridge?.handleMouseUpEvent()
        }

        func beginTerrainPaint(at point: CGPoint,
                               viewSize: CGSize,
                               entityUUID: String,
                               layer: Int,
                               radius: Float,
                               hardness: Float,
                               strength: Float,
                               spacing: Float,
                               maskPreset: Int,
                               maskPath: String,
                               autoNormalize: Bool,
                               invert: Bool) {
            guard let metalView = metalView else { return }
            let scale = metalView.layer?.contentsScale ?? 2.0
            let x = Float(point.x * scale)
            let y = Float(point.y * scale)
            let width = Float(viewSize.width * scale)
            let height = Float(viewSize.height * scale)
            bridge?.beginTerrainPaint(
                entity: entityUUID,
                x: x,
                y: y,
                screenWidth: width,
                screenHeight: height,
                layer: layer,
                radius: radius,
                hardness: hardness,
                strength: strength,
                spacing: spacing,
                maskPreset: maskPreset,
                maskPath: maskPath,
                autoNormalize: autoNormalize,
                invert: invert
            )
        }

        func updateTerrainPaint(at point: CGPoint,
                                viewSize: CGSize,
                                entityUUID: String,
                                layer: Int,
                                radius: Float,
                                hardness: Float,
                                strength: Float,
                                spacing: Float,
                                maskPreset: Int,
                                maskPath: String,
                                autoNormalize: Bool,
                                invert: Bool) {
            guard let metalView = metalView else { return }
            let scale = metalView.layer?.contentsScale ?? 2.0
            let x = Float(point.x * scale)
            let y = Float(point.y * scale)
            let width = Float(viewSize.width * scale)
            let height = Float(viewSize.height * scale)
            bridge?.updateTerrainPaint(
                entity: entityUUID,
                x: x,
                y: y,
                screenWidth: width,
                screenHeight: height,
                layer: layer,
                radius: radius,
                hardness: hardness,
                strength: strength,
                spacing: spacing,
                maskPreset: maskPreset,
                maskPath: maskPath,
                autoNormalize: autoNormalize,
                invert: invert
            )
        }

        func updateTerrainBrushPreview(at point: CGPoint,
                                       viewSize: CGSize,
                                       entityUUID: String,
                                       layer: Int,
                                       radius: Float,
                                       hardness: Float,
                                       maskPreset: Int,
                                       maskPath: String) {
            guard let metalView = metalView else { return }
            let scale = metalView.layer?.contentsScale ?? 2.0
            let x = Float(point.x * scale)
            let y = Float(point.y * scale)
            let width = Float(viewSize.width * scale)
            let height = Float(viewSize.height * scale)
            bridge?.updateTerrainBrushPreview(
                entity: entityUUID,
                x: x,
                y: y,
                screenWidth: width,
                screenHeight: height,
                layer: layer,
                radius: radius,
                hardness: hardness,
                maskPreset: maskPreset,
                maskPath: maskPath
            )
        }

        func endTerrainPaint() {
            bridge?.endTerrainPaint()
        }

        func clearTerrainBrushPreview() {
            bridge?.clearTerrainBrushPreview()
        }

        func setInputMonitoring(active: Bool) {
            if active {
                installInputMonitorsIfNeeded()
            } else {
                removeInputMonitors()
            }
        }

        private func installInputMonitorsIfNeeded() {
            if keyDownMonitor == nil {
                keyDownMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown]) { [weak self] event in
                    guard let self = self else { return event }
                    guard self.shouldCaptureKeyboardEvent() else { return event }
                    self.handleKeyDown(event.keyCode)
                    return nil
                }
            }
            if keyUpMonitor == nil {
                keyUpMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyUp]) { [weak self] event in
                    guard let self = self else { return event }
                    guard self.shouldCaptureKeyboardEvent() else { return event }
                    self.handleKeyUp(event.keyCode)
                    return nil
                }
            }
            if flagsChangedMonitor == nil {
                flagsChangedMonitor = NSEvent.addLocalMonitorForEvents(matching: [.flagsChanged]) { [weak self] event in
                    guard let self = self else { return event }
                    guard self.shouldCaptureKeyboardEvent() else { return event }
                    self.handleModifierEvent(event)
                    return event
                }
            }
        }

        private func removeInputMonitors() {
            if let monitor = keyDownMonitor {
                NSEvent.removeMonitor(monitor)
                keyDownMonitor = nil
            }
            if let monitor = keyUpMonitor {
                NSEvent.removeMonitor(monitor)
                keyUpMonitor = nil
            }
            if let monitor = flagsChangedMonitor {
                NSEvent.removeMonitor(monitor)
                flagsChangedMonitor = nil
            }
        }

        private func shouldCaptureKeyboardEvent() -> Bool {
            guard let window = metalView?.window else { return false }
            if window.firstResponder is NSTextView {
                return false
            }
            return true
        }

        private func handleModifierEvent(_ event: NSEvent) {
            switch event.keyCode {
            case 56, 60:
                if event.modifierFlags.contains(.shift) {
                    handleKeyDown(event.keyCode)
                } else {
                    handleKeyUp(event.keyCode)
                }
            case 59, 62:
                if event.modifierFlags.contains(.control) {
                    handleKeyDown(event.keyCode)
                } else {
                    handleKeyUp(event.keyCode)
                }
            case 58, 61:
                if event.modifierFlags.contains(.option) {
                    handleKeyDown(event.keyCode)
                } else {
                    handleKeyUp(event.keyCode)
                }
            case 55, 54:
                if event.modifierFlags.contains(.command) {
                    handleKeyDown(event.keyCode)
                } else {
                    handleKeyUp(event.keyCode)
                }
            default:
                break
            }
        }
    }
}

// Custom NSView with CAMetalLayer and input handling
class MetalDisplayView: NSView {
    var metalLayer: CAMetalLayer?
    weak var inputDelegate: InputDelegate?
    weak var coordinator: MetalView.Coordinator?
    var allowsPicking: Bool = true
    var allowsCameraControl: Bool = true
    var terrainPaintEnabled: Bool = false {
        didSet {
            if oldValue && !terrainPaintEnabled {
                coordinator?.endTerrainPaint()
                coordinator?.clearTerrainBrushPreview()
            }
        }
    }
    var terrainPaintTargetUUID: String = "" {
        didSet {
            if oldValue != terrainPaintTargetUUID {
                coordinator?.endTerrainPaint()
                coordinator?.clearTerrainBrushPreview()
            }
        }
    }
    var terrainPaintLayer: Int = 0
    var terrainBrushRadius: Float = 1.5
    var terrainBrushHardness: Float = 0.65
    var terrainBrushStrength: Float = 0.35
    var terrainBrushSpacing: Float = 0.25
    var terrainBrushMaskPreset: Int = 0
    var terrainBrushMaskPath: String = ""
    var terrainBrushAutoNormalize: Bool = true
    private var trackingArea: NSTrackingArea?
    private var isRightMouseDown: Bool = false
    private var isLeftMouseDown: Bool = false
    
    override var acceptsFirstResponder: Bool { return true }
    
    override init(frame: NSRect) {
        super.init(frame: frame)
        setupMetalLayer()
    }
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupMetalLayer()
    }
    
    override func makeBackingLayer() -> CALayer {
        let layer = CAMetalLayer()
        self.metalLayer = layer
        return layer
    }
    
    override var wantsUpdateLayer: Bool {
        return true
    }
    
    private func setupMetalLayer() {
        wantsLayer = true
        layer = makeBackingLayer()
        
        if let metalLayer = layer as? CAMetalLayer {
            self.metalLayer = metalLayer
            metalLayer.device = MTLCreateSystemDefaultDevice()
            metalLayer.pixelFormat = .bgra8Unorm
            metalLayer.framebufferOnly = false
            metalLayer.contentsScale = window?.backingScaleFactor
                ?? NSScreen.main?.backingScaleFactor
                ?? 2.0
        }
    }

    private func updateDrawableSize(_ newSize: NSSize) {
        guard let metalLayer = metalLayer, newSize.width > 0 && newSize.height > 0 else {
            return
        }

        let scale = metalLayer.contentsScale
        let drawableSize = CGSize(
            width: ceil(newSize.width * scale),
            height: ceil(newSize.height * scale)
        )

        metalLayer.frame = bounds
        metalLayer.drawableSize = drawableSize
    }
    
    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        updateDrawableSize(newSize)
        
        if newSize.width > 0 && newSize.height > 0 {
            coordinator?.handleResize(CGSize(width: newSize.width, height: newSize.height))
        }
    }

    override func setBoundsSize(_ newSize: NSSize) {
        super.setBoundsSize(newSize)
        updateDrawableSize(newSize)

        if newSize.width > 0 && newSize.height > 0 {
            coordinator?.handleResize(CGSize(width: newSize.width, height: newSize.height))
        }
    }

    override func layout() {
        super.layout()
        updateDrawableSize(bounds.size)
        if bounds.size.width > 0 && bounds.size.height > 0 {
            coordinator?.handleResize(bounds.size)
        }
    }
    
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        
        if window != nil {
            if let metalLayer = metalLayer {
                metalLayer.contentsScale = window?.backingScaleFactor
                    ?? NSScreen.main?.backingScaleFactor
                    ?? metalLayer.contentsScale
            }
            // Update drawable size when added to window
            setFrameSize(bounds.size)
            
            // Become first responder to receive keyboard events
            window?.makeFirstResponder(self)
            
            // Setup mouse tracking
            updateTrackingAreas()
        }
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        if let metalLayer = metalLayer {
            metalLayer.contentsScale = window?.backingScaleFactor
                ?? NSScreen.main?.backingScaleFactor
                ?? metalLayer.contentsScale
            updateDrawableSize(bounds.size)
        }
    }
    
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        
        // Remove old tracking area
        if let existingArea = trackingArea {
            removeTrackingArea(existingArea)
        }
        
        // Create new tracking area with proper options
        let options: NSTrackingArea.Options = [
            .mouseMoved,
            .mouseEnteredAndExited,
            .activeAlways,
            .inVisibleRect,
            .enabledDuringMouseDrag
        ]
        trackingArea = NSTrackingArea(rect: bounds, options: options, owner: self, userInfo: nil)
        if let trackingArea = trackingArea {
            addTrackingArea(trackingArea)
        }
    }
    
    // MARK: - Keyboard Input
    
    override func keyDown(with event: NSEvent) {
        inputDelegate?.handleKeyDown(event.keyCode)
    }
    
    override func keyUp(with event: NSEvent) {
        inputDelegate?.handleKeyUp(event.keyCode)
    }
    
    // MARK: - Mouse Input (Unity/Unreal style)
    
    override func rightMouseDragged(with event: NSEvent) {
        guard inputDelegate != nil else { return }
        // Send mouse movement to camera controller
        inputDelegate?.handleMouseMove(deltaX: Float(event.deltaX), deltaY: Float(event.deltaY))
    }
    
    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        isLeftMouseDown = true
        let point = convert(event.locationInWindow, from: nil)
        let invert = event.modifierFlags.contains(.option)

        if terrainPaintEnabled && !terrainPaintTargetUUID.isEmpty {
            coordinator?.beginTerrainPaint(
                at: point,
                viewSize: bounds.size,
                entityUUID: terrainPaintTargetUUID,
                layer: terrainPaintLayer,
                radius: terrainBrushRadius,
                hardness: terrainBrushHardness,
                strength: terrainBrushStrength,
                spacing: terrainBrushSpacing,
                maskPreset: terrainBrushMaskPreset,
                maskPath: terrainBrushMaskPath,
                autoNormalize: terrainBrushAutoNormalize,
                invert: invert
            )
            return
        }

        inputDelegate?.handleMouseButton(0, pressed: true)
        guard allowsPicking else { return }
        
        // Get click position
        let additive = event.modifierFlags.contains(.command)
        coordinator?.handleMouseClick(at: point, viewSize: bounds.size, additive: additive)
    }
    
    override func mouseDragged(with event: NSEvent) {
        if terrainPaintEnabled && !terrainPaintTargetUUID.isEmpty && isLeftMouseDown {
            let point = convert(event.locationInWindow, from: nil)
            let invert = event.modifierFlags.contains(.option)
            coordinator?.updateTerrainBrushPreview(
                at: point,
                viewSize: bounds.size,
                entityUUID: terrainPaintTargetUUID,
                layer: terrainPaintLayer,
                radius: terrainBrushRadius,
                hardness: terrainBrushHardness,
                maskPreset: terrainBrushMaskPreset,
                maskPath: terrainBrushMaskPath
            )
            coordinator?.updateTerrainPaint(
                at: point,
                viewSize: bounds.size,
                entityUUID: terrainPaintTargetUUID,
                layer: terrainPaintLayer,
                radius: terrainBrushRadius,
                hardness: terrainBrushHardness,
                strength: terrainBrushStrength,
                spacing: terrainBrushSpacing,
                maskPreset: terrainBrushMaskPreset,
                maskPath: terrainBrushMaskPath,
                autoNormalize: terrainBrushAutoNormalize,
                invert: invert
            )
            return
        }
        guard allowsPicking else { return }
        if isLeftMouseDown {
            // Get current position
            let point = convert(event.locationInWindow, from: nil)
            coordinator?.handleMouseDrag(at: point, viewSize: bounds.size)
        }
    }
    
    override func mouseUp(with event: NSEvent) {
        isLeftMouseDown = false
        if terrainPaintEnabled && !terrainPaintTargetUUID.isEmpty {
            coordinator?.endTerrainPaint()
            return
        }
        inputDelegate?.handleMouseButton(0, pressed: false)
        guard allowsPicking else { return }
        coordinator?.handleMouseUpEvent()
    }

    override func mouseMoved(with event: NSEvent) {
        guard terrainPaintEnabled, !terrainPaintTargetUUID.isEmpty else { return }
        let point = convert(event.locationInWindow, from: nil)
        coordinator?.updateTerrainBrushPreview(
            at: point,
            viewSize: bounds.size,
            entityUUID: terrainPaintTargetUUID,
            layer: terrainPaintLayer,
            radius: terrainBrushRadius,
            hardness: terrainBrushHardness,
            maskPreset: terrainBrushMaskPreset,
            maskPath: terrainBrushMaskPath
        )
    }

    override func mouseExited(with event: NSEvent) {
        coordinator?.clearTerrainBrushPreview()
    }
    
    override func rightMouseDown(with event: NSEvent) {
        guard inputDelegate != nil else { return }
        window?.makeFirstResponder(self)
        isRightMouseDown = true
        
        // Hide cursor (Unity/Unreal style)
        if allowsCameraControl {
            NSCursor.hide()
        }
        inputDelegate?.handleMouseButton(1, pressed: true)
    }
    
    override func rightMouseUp(with event: NSEvent) {
        guard inputDelegate != nil else { return }
        isRightMouseDown = false
        
        // Show cursor again
        if allowsCameraControl {
            NSCursor.unhide()
        }
        inputDelegate?.handleMouseButton(1, pressed: false)
    }
}
