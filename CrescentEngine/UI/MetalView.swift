import SwiftUI
import MetalKit
import QuartzCore

// Protocol for input
protocol InputDelegate: AnyObject {
    func handleKeyDown(_ keyCode: UInt16)
    func handleKeyUp(_ keyCode: UInt16)
    func handleMouseMove(deltaX: Float, deltaY: Float)
    func handleMouseButton(_ button: Int, pressed: Bool)
}

struct MetalView: NSViewRepresentable {
    typealias NSViewType = MetalDisplayView
    
    func makeCoordinator() -> Coordinator {
        Coordinator()
    }
    
    func makeNSView(context: Context) -> MetalDisplayView {
        let metalView = MetalDisplayView()
        metalView.coordinator = context.coordinator
        context.coordinator.metalView = metalView
        
        // Initialize engine (delayed until view is laid out)
        DispatchQueue.main.async {
            context.coordinator.initializeEngine()
        }
        
        return metalView
    }
    
    func updateNSView(_ nsView: MetalDisplayView, context: Context) {
        // Handle view updates - resize if needed
        if nsView.bounds.size.width > 0 && nsView.bounds.size.height > 0 {
            context.coordinator.handleResize(nsView.bounds.size)
        }
    }
    
    class Coordinator: NSObject, InputDelegate {
        var bridge: CrescentEngineBridge?
        var metalView: MetalDisplayView?
        private var displayLink: Any? // Can be CVDisplayLink or CADisplayLink
        private var lastTime: CFTimeInterval = 0
        private var isEngineInitialized = false
        
        func initializeEngine() {
            guard !isEngineInitialized else { return }
            guard let metalView = metalView else { return }
            
            // Set input delegate
            metalView.inputDelegate = self
            
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
            if bridge.initialize() {
                print("Engine initialized successfully from Swift")
                
                // Set metal layer
                if let metalLayer = metalView.metalLayer {
                    print("Setting metal layer...")
                    bridge.setMetalLayer(metalLayer)
                }
                
                self.bridge = bridge
                self.isEngineInitialized = true
                
                // Setup display link
                setupDisplayLink()
                
                // Print mouse instructions
                print("CONTROLS:")
                print("  HOLD RIGHT-CLICK and move mouse to look around")
                print("  WASD to move, QE for up/down, Shift to sprint")
            }
        }
        
        func handleResize(_ size: CGSize) {
            guard let metalView = metalView else { return }
            guard let bridge = bridge else { return }
            
            let scale = metalView.layer?.contentsScale ?? 2.0
            let width = Float(size.width * scale)
            let height = Float(size.height * scale)
            
            if width > 0 && height > 0 {
                bridge.resize(withWidth: width, height: height)
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
            guard let bridge = bridge, isEngineInitialized else { return }
            
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
    }
}

// Custom NSView with CAMetalLayer and input handling
class MetalDisplayView: NSView {
    var metalLayer: CAMetalLayer?
    weak var inputDelegate: InputDelegate?
    weak var coordinator: MetalView.Coordinator?
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
    
    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        
        if let metalLayer = metalLayer, newSize.width > 0 && newSize.height > 0 {
            let scale = metalLayer.contentsScale
            let drawableSize = CGSize(
                width: newSize.width * scale,
                height: newSize.height * scale
            )
            
            print("Setting drawable size: \(drawableSize)")
            metalLayer.drawableSize = drawableSize
        }
        
        if newSize.width > 0 && newSize.height > 0 {
            coordinator?.handleResize(CGSize(width: newSize.width, height: newSize.height))
        }
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
        // Send mouse movement to camera controller
        inputDelegate?.handleMouseMove(deltaX: Float(event.deltaX), deltaY: Float(event.deltaY))
    }
    
    override func mouseDown(with event: NSEvent) {
        isLeftMouseDown = true
        inputDelegate?.handleMouseButton(0, pressed: true)
        
        // Get click position
        let point = convert(event.locationInWindow, from: nil)
        let additive = event.modifierFlags.contains(.command)
        coordinator?.handleMouseClick(at: point, viewSize: bounds.size, additive: additive)
    }
    
    override func mouseDragged(with event: NSEvent) {
        if isLeftMouseDown {
            // Get current position
            let point = convert(event.locationInWindow, from: nil)
            coordinator?.handleMouseDrag(at: point, viewSize: bounds.size)
        }
    }
    
    override func mouseUp(with event: NSEvent) {
        isLeftMouseDown = false
        inputDelegate?.handleMouseButton(0, pressed: false)
        coordinator?.handleMouseUpEvent()
    }
    
    override func rightMouseDown(with event: NSEvent) {
        isRightMouseDown = true
        
        // Hide cursor (Unity/Unreal style)
        NSCursor.hide()
        
        print("Camera control active - Mouse hidden")
        inputDelegate?.handleMouseButton(1, pressed: true)
    }
    
    override func rightMouseUp(with event: NSEvent) {
        isRightMouseDown = false
        
        // Show cursor again
        NSCursor.unhide()
        
        print("Camera control released - Mouse visible")
        inputDelegate?.handleMouseButton(1, pressed: false)
    }
}

