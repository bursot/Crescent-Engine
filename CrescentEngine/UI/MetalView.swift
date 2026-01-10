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

    init(viewKind: RenderViewKind, isActive: Bool, drivesLoop: Bool = false) {
        self.viewKind = viewKind
        self.isActive = isActive
        self.drivesLoop = drivesLoop
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
        context.coordinator.metalView = metalView
        
        // Initialize engine (delayed until view is laid out)
        DispatchQueue.main.async {
            context.coordinator.initializeEngine()
        }
        
        return metalView
    }
    
    func updateNSView(_ nsView: MetalDisplayView, context: Context) {
        // Handle view updates - resize if needed
        context.coordinator.applyMetalLayerIfNeeded()
        if nsView.bounds.size.width > 0 && nsView.bounds.size.height > 0 {
            context.coordinator.handleResize(nsView.bounds.size)
        }
        nsView.allowsPicking = (viewKind == .scene) && isActive
        nsView.allowsCameraControl = isActive
        nsView.inputDelegate = isActive ? context.coordinator : nil
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
            
            print("Initializing engine with view size: \(metalView.bounds.size)")
            
            // Initialize engine
            let bridge = CrescentEngineBridge.shared()
            if !Coordinator.engineInitialized {
                Coordinator.engineInitialized = bridge.initialize()
            }
            if Coordinator.engineInitialized {
                print("Engine initialized successfully from Swift")
                
                // Set metal layer
                if let metalLayer = metalView.metalLayer {
                    print("Setting metal layer...")
                    switch viewKind {
                    case .scene:
                        bridge.setSceneMetalLayer(metalLayer)
                    case .game:
                        bridge.setGameMetalLayer(metalLayer)
                    }
                }
                
                self.bridge = bridge
                self.isEngineInitialized = Coordinator.engineInitialized

                handleResize(metalView.bounds.size)
                
                // Setup display link
                if drivesLoop {
                    setupDisplayLink()
                }
                
                if drivesLoop {
                    // Print mouse instructions
                    print("CONTROLS:")
                    print("  HOLD RIGHT-CLICK and move mouse to look around")
                    print("  WASD to move, QE for up/down, Shift to sprint")
                }
            }
        }
        
        func handleResize(_ size: CGSize) {
            guard let metalView = metalView else { return }
            guard let bridge = bridge else { return }

            let drawableSize = metalView.metalLayer?.drawableSize ?? .zero
            let scale = metalView.layer?.contentsScale ?? 2.0
            let width = Float(drawableSize.width > 0 ? drawableSize.width : size.width * scale)
            let height = Float(drawableSize.height > 0 ? drawableSize.height : size.height * scale)

            if width > 0 && height > 0 {
                switch viewKind {
                case .scene:
                    bridge.resizeScene(withWidth: width, height: height)
                case .game:
                    bridge.resizeGame(withWidth: width, height: height)
                }
            }
        }

        func applyMetalLayerIfNeeded() {
            guard let bridge = bridge, let metalLayer = metalView?.metalLayer else { return }
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
                print("Display link started (modern API)")
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
                
                print("Display link started (CVDisplayLink)")
            }
        }
        
        @objc func renderFrameFromDisplayLink(_ displayLink: CADisplayLink) {
            renderFrame()
        }
        
        func renderFrame() {
            guard drivesLoop, let bridge = bridge, isEngineInitialized else { return }
            
            let currentTime = CACurrentMediaTime()
            let deltaTime = lastTime == 0 ? 0.016 : Float(currentTime - lastTime)
            lastTime = currentTime
            
            // Update and render
            bridge.update(deltaTime)
            bridge.render()
        }
        
        deinit {
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
            metalLayer.contentsScale = NSScreen.main?.backingScaleFactor ?? 2.0
            
            print("Metal layer created with scale: \(metalLayer.contentsScale)")
        }
    }

    private func updateDrawableSize(_ newSize: NSSize) {
        guard let metalLayer = metalLayer, newSize.width > 0 && newSize.height > 0 else {
            return
        }

        let scale = metalLayer.contentsScale
        let drawableSize = CGSize(
            width: newSize.width * scale,
            height: newSize.height * scale
        )

        metalLayer.frame = bounds
        metalLayer.drawableSize = drawableSize
        print("Setting drawable size: \(drawableSize)")
    }
    
    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        updateDrawableSize(newSize)
        
        if newSize.width > 0 && newSize.height > 0 {
            coordinator?.handleResize(CGSize(width: newSize.width, height: newSize.height))
        }
    }

    override func layout() {
        super.layout()
        updateDrawableSize(bounds.size)
    }
    
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        
        if window != nil {
            // Update drawable size when added to window
            setFrameSize(bounds.size)
            
            // Become first responder to receive keyboard events
            window?.makeFirstResponder(self)
            
            // Setup mouse tracking
            updateTrackingAreas()
            
            print("MetalView added to window - Ready to receive input")
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
            .activeAlways,
            .inVisibleRect,
            .enabledDuringMouseDrag
        ]
        trackingArea = NSTrackingArea(rect: bounds, options: options, owner: self, userInfo: nil)
        if let trackingArea = trackingArea {
            addTrackingArea(trackingArea)
        }
        
        print("Mouse tracking area updated: \(bounds)")
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
        inputDelegate?.handleMouseButton(0, pressed: true)
        guard allowsPicking else { return }
        
        // Get click position
        let point = convert(event.locationInWindow, from: nil)
        let additive = event.modifierFlags.contains(.command)
        coordinator?.handleMouseClick(at: point, viewSize: bounds.size, additive: additive)
    }
    
    override func mouseDragged(with event: NSEvent) {
        guard allowsPicking else { return }
        if isLeftMouseDown {
            // Get current position
            let point = convert(event.locationInWindow, from: nil)
            coordinator?.handleMouseDrag(at: point, viewSize: bounds.size)
        }
    }
    
    override func mouseUp(with event: NSEvent) {
        isLeftMouseDown = false
        inputDelegate?.handleMouseButton(0, pressed: false)
        guard allowsPicking else { return }
        coordinator?.handleMouseUpEvent()
    }
    
    override func rightMouseDown(with event: NSEvent) {
        guard inputDelegate != nil else { return }
        window?.makeFirstResponder(self)
        isRightMouseDown = true
        
        // Hide cursor (Unity/Unreal style)
        if allowsCameraControl {
            NSCursor.hide()
            print("Camera control active - Mouse hidden")
        }
        inputDelegate?.handleMouseButton(1, pressed: true)
    }
    
    override func rightMouseUp(with event: NSEvent) {
        guard inputDelegate != nil else { return }
        isRightMouseDown = false
        
        // Show cursor again
        if allowsCameraControl {
            NSCursor.unhide()
            print("Camera control released - Mouse visible")
        }
        inputDelegate?.handleMouseButton(1, pressed: false)
    }
}
