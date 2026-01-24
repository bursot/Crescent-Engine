# CrescentEngine

A modern, real-time 3D game engine and editor for macOS, built with Metal, C++17/20, and SwiftUI.

<img width="1512" height="982" alt="Ekran Resmi 2026-01-24 03 02 04" src="https://github.com/user-attachments/assets/90cd4f22-fca4-4b27-bb47-95a8952df112" />

<img width="1512" height="982" alt="Ekran Resmi 2026-01-24 03 02 45" src="https://github.com/user-attachments/assets/20b71f9f-126a-473e-abf5-083dd4490da6" />


<img width="1624" height="1001" alt="Ekran Resmi 2026-01-11 23 30 11" src="https://github.com/user-attachments/assets/8aaa2dce-6dcd-4a69-ab87-c17e4eea0177" />

<img width="1624" height="1001" alt="Ekran Resmi 2026-01-08 18 48 05" src="https://github.com/user-attachments/assets/44671059-f3dd-4032-8aa0-62fd67162dad" /><img width="1624" height="1001" alt="Ekran Resmi 2026-01-08 18 42 30" src="https://github.com/user-attachments/assets/8312aaac-450a-4b3b-b03e-1a00a18a4fc0" />


## Overview

CrescentEngine is a comprehensive game engine featuring a complete editor, advanced rendering pipeline, physics simulation, animation system, and asset management. It combines the performance of C++ with the modern UI capabilities of SwiftUI to deliver a native macOS development experience.

## Key Features

### Rendering
- **Metal-based PBR Pipeline** - Physically Based Rendering with industry-standard materials
- **Clustered Lighting** - Efficient rendering of hundreds of lights using GPU compute shaders
- **Advanced Shadows** - Cascaded shadow maps for directional lights, cube maps for point lights
- **Post-Processing** - SSAO, SSR, Bloom, TAA, Motion Blur, DOF, Volumetric Fog, Color Grading
- **IBL System** - Image-Based Lighting with prefiltered environment maps and BRDF LUT
- **Skinned Mesh Rendering** - Full skeletal animation support with bone matrices

### Architecture
- **ECS (Entity-Component-System)** - Modular, flexible game object management
- **Scene Management** - Multiple scenes, play/editor mode separation
- **Component System** - Camera, Light, MeshRenderer, SkinnedMeshRenderer, Animator, Physics, etc.
- **Hierarchical Transforms** - Parent-child relationships with world/local space

### Physics
- **Jolt Physics Integration** - Industry-standard physics engine
- **Rigidbody Dynamics** - Static, Kinematic, and Dynamic bodies
- **Collision Detection** - Box, Sphere, Capsule shapes with collision layers
- **Character Controllers** - Physics-based character movement
- **Contact Events** - Enter, Stay, Exit callbacks

### Animation
- **Skeletal Animation** - Full bone hierarchy support
- **Animation Clips** - Keyframe-based animation with events
- **Animator System** - State machines, blend trees, parameters (float, int, bool, trigger)
- **Root Motion** - Automatic character movement from animation
- **IK Constraints** - Two-bone IK solver for procedural posing

### Editor
- **SwiftUI Interface** - Modern, native macOS editor
- **Scene Hierarchy** - Visual entity tree with drag-and-drop
- **Inspector Panel** - Real-time property editing
- **Asset Browser** - GUID-based asset management
- **Gizmo System** - Translate, Rotate, Scale manipulation
- **Multi-Window Support** - Scene view, Game view, Settings, Animation windows

### Asset Pipeline
- **Multi-Format Import** - FBX, OBJ, GLTF, GLB, DAE, and more via Assimp
- **Texture Support** - PNG, JPG, TGA, EXR, HDR with mipmap generation
- **HDRI Import** - Environment map support with IBL generation
- **GUID System** - Unique asset identification and reference tracking
- **Import Cache** - Optimized asset processing and caching

## Architecture

### System Layers

```mermaid
graph TB
    subgraph "UI Layer (Swift/SwiftUI)"
        A1[CrescentEngineApp]
        A2[ContentView]
        A3[MetalView]
        A4[HierarchyPanel]
        A5[InspectorPanel]
        A6[AssetBrowserPanel]
    end
    
    subgraph "Bridge Layer (Objective-C++)"
        B1[CrescentEngineBridge]
    end
    
    subgraph "Core Engine (C++)"
        C1[Engine]
        C2[SceneManager]
        C3[Renderer]
        C4[InputManager]
    end
    
    subgraph "ECS Framework"
        D1[Entity]
        D2[Component]
        D3[Transform]
    end
    
    subgraph "Scene System"
        E1[Scene]
        E2[SceneCommands]
        E3[SceneSerializer]
    end
    
    subgraph "Rendering System"
        F1[Renderer]
        F2[LightingSystem]
        F3[ShadowRenderPass]
        F4[ClusteredLightingPass]
    end
    
    subgraph "Components"
        G1[Camera/Light/MeshRenderer]
        G2[SkinnedMeshRenderer/Animator]
        G3[Rigidbody/PhysicsCollider]
    end
    
    subgraph "Physics System"
        H1[PhysicsWorld]
        H2[Jolt Physics]
    end
    
    subgraph "Asset System"
        I1[AssetDatabase]
        I2[TextureLoader]
        I3[IBLGenerator]
    end
    
    subgraph "Metal GPU"
        J1[Metal Device]
        J2[Command Queue]
        J3[Shaders]
    end
    
    A1 --> A2
    A2 --> A3
    A3 --> B1
    B1 --> C1
    C1 --> C2
    C1 --> C3
    C1 --> C4
    C2 --> E1
    E1 --> D1
    D1 --> D2
    D2 --> G1
    D2 --> G2
    D2 --> G3
    C3 --> F1
    F1 --> F2
    F1 --> F3
    F1 --> F4
    E1 --> H1
    H1 --> H2
    E1 --> I1
    I1 --> I2
    I1 --> I3
    F1 --> J1
    J1 --> J2
    J2 --> J3
    
    style A1 fill:#FF6B6B
    style B1 fill:#4ECDC4
    style C1 fill:#45B7D1
    style D1 fill:#96CEB4
    style E1 fill:#FFEAA7
    style F1 fill:#DDA15E
    style G1 fill:#BC6C25
    style H1 fill:#6C5CE7
    style I1 fill:#A29BFE
    style J1 fill:#FD79A8
```

### Render Pipeline

```mermaid
flowchart TB
    subgraph "Frame Start"
        Start[Frame Start<br/>60 FPS]
    end
    
    subgraph "Shadow Generation"
        ShadowStart[Shadow Pass] --> DirShadow[Directional Shadows<br/>4 Cascades]
        DirShadow --> PointShadow[Point Light Shadows<br/>Cube Maps]
        PointShadow --> SpotShadow[Spot Light Shadows]
        SpotShadow --> ShadowAtlas[Shadow Atlas<br/>4096x4096]
    end
    
    subgraph "Geometry Passes"
        PrepassStart[Prepass] --> DepthPass[Depth Prepass<br/>Early Z-Culling]
        DepthPass --> NormalPass[Normal Pass<br/>View Space]
        NormalPass --> DepthBuffer[Depth Buffer]
        NormalPass --> NormalBuffer[Normal Buffer]
        
        VelocityStart[Velocity Pass] --> VelocityCalc[Motion Vectors]
        VelocityCalc --> VelocityBuffer[Velocity Buffer]
    end
    
    subgraph "Lighting Setup"
        LightGather[Light Gathering] --> ClusterBuild[Clustered Lighting<br/>16x9x24 Grid]
        ClusterBuild --> ClusterHeaders[Cluster Headers]
        ClusterBuild --> LightIndices[Light Indices]
    end
    
    subgraph "Main Rendering"
        MainStart[Main Pass] --> OpaquePass[Opaque Objects<br/>PBR]
        OpaquePass --> SkinnedPass[Skinned Meshes]
        SkinnedPass --> DecalPass[Decal Projection]
        DecalPass --> ColorBuffer[Color Buffer<br/>HDR]
        
        TransparentStart[Transparent Pass] --> TransparentRender[Transparent Objects]
        TransparentRender --> ColorBuffer
    end
    
    subgraph "Post-Processing"
        PostStart[Post-Processing] --> SSAOPass[SSAO]
        SSAOPass --> SSRFass[SSR]
        SSRFass --> BloomStart[Bloom]
        BloomStart --> TAAStart[TAA]
        TAAStart --> MotionBlurStart[Motion Blur]
        MotionBlurStart --> DOFStart[DOF]
        DOFStart --> FogStart[Volumetric Fog]
        FogStart --> ColorGradingStart[Color Grading]
        ColorGradingStart --> FinalOutput[Final Frame]
    end
    
    Start --> ShadowStart
    ShadowStart --> PrepassStart
    PrepassStart --> VelocityStart
    VelocityStart --> LightGather
    LightGather --> MainStart
    MainStart --> TransparentStart
    TransparentStart --> PostStart
    
    ShadowAtlas -.->|Sample| OpaquePass
    DepthBuffer -.->|Input| SSAOPass
    NormalBuffer -.->|Input| SSAOPass
    VelocityBuffer -.->|Input| TAAStart
    VelocityBuffer -.->|Input| MotionBlurStart
    ClusterHeaders -.->|Sample| OpaquePass
    LightIndices -.->|Sample| OpaquePass
    
    style Start fill:#FF6B6B,stroke:#333,stroke-width:3px
    style ShadowStart fill:#4ECDC4,stroke:#333,stroke-width:2px
    style PrepassStart fill:#45B7D1,stroke:#333,stroke-width:2px
    style MainStart fill:#FFEAA7,stroke:#333,stroke-width:2px
    style PostStart fill:#BC6C25,stroke:#333,stroke-width:2px
    style FinalOutput fill:#00B894,stroke:#333,stroke-width:4px
```

### ECS System

```mermaid
classDiagram
    class Scene {
        +UUID uuid
        +string name
        +vector~Entity~ entities
        +PhysicsWorld* physicsWorld
        +createEntity()
        +OnUpdate(float)
    }
    
    class Entity {
        +UUID uuid
        +string name
        +Transform* transform
        +map~TypeIndex,Component~ components
        +addComponent~T~()
        +getComponent~T~()
        +OnUpdate(float)
    }
    
    class Component {
        <<abstract>>
        +Entity* entity
        +bool enabled
        +OnCreate()
        +OnUpdate(float)
    }
    
    class Transform {
        +Vector3 position
        +Quaternion rotation
        +Vector3 scale
        +getWorldMatrix()
    }
    
    class Camera {
        +ProjectionType type
        +Matrix4x4 viewMatrix
    }
    
    class MeshRenderer {
        +Mesh* mesh
        +Material* material
    }
    
    class Light {
        +LightType type
        +Vector3 color
        +bool castShadows
    }
    
    class Rigidbody {
        +RigidbodyType type
        +float mass
    }
    
    Scene "1" *-- "*" Entity : contains
    Entity "1" *-- "1" Transform : has
    Entity "1" *-- "*" Component : has
    Component <|-- Camera
    Component <|-- MeshRenderer
    Component <|-- Light
    Component <|-- Rigidbody
    Transform "1" *-- "*" Transform : parent-child
```

### Frame Loop

```mermaid
sequenceDiagram
    participant CVDisplayLink
    participant MetalView
    participant Bridge
    participant Engine
    participant SceneManager
    participant Scene
    participant Components
    participant Renderer
    participant GPU
    
    CVDisplayLink->>MetalView: 60 FPS Callback
    MetalView->>Bridge: update(deltaTime)
    Bridge->>Engine: update(deltaTime)
    Engine->>SceneManager: update(deltaTime)
    SceneManager->>Scene: OnUpdate(deltaTime)
    Scene->>Components: OnUpdate(deltaTime)
    Components->>Components: Process Logic
    
    MetalView->>Bridge: render()
    Bridge->>Engine: render()
    Engine->>Renderer: renderScene()
    Renderer->>Renderer: Shadow Pass
    Renderer->>Renderer: Prepass
    Renderer->>Renderer: Main Pass
    Renderer->>Renderer: Post-Processing
    Renderer->>GPU: Submit Command Buffer
    GPU->>GPU: Execute Rendering
    GPU-->>MetalView: Present Frame
```

### Clustered Lighting System

```mermaid
flowchart TD
    A[Viewport<br/>1920x1080] -->|Divide| B[Cluster Grid<br/>16x9x24 = 3456 Clusters]
    B -->|Compute Shader| C[cluster_build]
    C -->|For Each Cluster| D[Light Culling]
    D -->|Check| E{Light in<br/>Cluster?}
    E -->|Yes| F[Add to Light List<br/>Max 64 per Cluster]
    E -->|No| G[Skip]
    F -->|Store| H[Cluster Header<br/>offset + count]
    F -->|Store| I[Light Index Buffer]
    H -->|GPU Buffer| J[PBR Shader]
    I -->|GPU Buffer| J
    J -->|Sample Lights| K[Final Lighting]
    
    style A fill:#FF6B6B
    style B fill:#4ECDC4
    style C fill:#45B7D1
    style D fill:#96CEB4
    style H fill:#FFEAA7
    style I fill:#DDA15E
    style K fill:#BC6C25
```

### Asset Pipeline

```mermaid
flowchart LR
    A[Asset File<br/>.fbx/.png/.hdr] -->|Import| B[AssetDatabase]
    B -->|Generate GUID| C[.cmeta File]
    B -->|Register| D[GUID Map]
    B -->|Cache| E[Library/ImportCache]
    
    F[Model Import] -->|Assimp| G[Parse Model]
    G -->|Extract| H[Meshes]
    G -->|Extract| I[Materials]
    G -->|Extract| J[Skeleton]
    G -->|Extract| K[Animations]
    H -->|Create| L[Entity + MeshRenderer]
    I -->|Assign| L
    J -->|Assign| M[SkinnedMeshRenderer]
    K -->|Assign| M
    
    N[Texture Import] -->|stb_image/tinyexr| O[Load Image]
    O -->|Create| P[Metal Texture]
    P -->|Cache| Q[TextureCache]
    
    R[HDRI Import] -->|Load EXR| S[Equirectangular]
    S -->|IBLGenerator| T[Cubemap]
    T -->|Prefilter| U[Prefiltered Env]
    T -->|Irradiance| V[Irradiance Map]
    
    style A fill:#FF6B6B
    style B fill:#4ECDC4
    style C fill:#45B7D1
    style L fill:#DDA15E
    style M fill:#BC6C25
    style P fill:#6C5CE7
    style T fill:#A29BFE
```

## Quick Start

### Requirements
- **macOS 14+** (Intel or Apple Silicon)
- **Xcode 15+** (Swift 5)
- **CMake 3.26+** and Git (for dependency builds)

### Installation

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/CrescentEngine.git
   cd CrescentEngine
   ```

2. **Setup dependencies:**
   ```bash
   ./scripts/setup_deps.sh
   ```
   This will build:
   - Jolt Physics (in `ThirdParty/jolt-build-debug` and `jolt-build-release`)
   - Assimp (in `ThirdParty/assimp-build-debug` and `assimp-build-release`)

3. **Open in Xcode:**
   ```bash
   open CrescentEngine.xcodeproj
   ```

4. **Build and Run:**
   - Select the `CrescentEngine` scheme
   - Choose Debug or Release configuration
   - Press ⌘R to build and run

## Features in Detail

### Rendering Features
- PBR (Physically Based Rendering)
- Clustered Lighting (16x9x24 grid, 3456 clusters)
- Cascaded Shadow Maps (4 levels)
- Point Light Cube Maps
- Spot Light Shadows
- Screen-Space Ambient Occlusion (SSAO)
- Screen-Space Reflections (SSR)
- Bloom (5 mip levels)
- Temporal Anti-Aliasing (TAA)
- Motion Blur
- Depth of Field
- Volumetric Fog
- Color Grading (3D LUT)
- Image-Based Lighting (IBL)
- Skinned Mesh Rendering

### Physics Features
- Rigidbody Dynamics (Static, Kinematic, Dynamic)
- Collision Shapes (Box, Sphere, Capsule)
- Collision Layers & Masks
- Contact Events (Enter, Stay, Exit)
- Character Controllers
- First-Person Controllers
- Trigger Volumes

### Animation Features
- Skeletal Animation
- Animation Clips with Events
- Animator State Machine
- Blend Trees (1D parameter-driven)
- Root Motion
- Two-Bone IK Solver
- Animation Parameters (Float, Int, Bool, Trigger)

### Editor Features
- Scene Hierarchy Panel
- Inspector Panel (real-time property editing)
- Asset Browser
- Gizmo System (Translate, Rotate, Scale)
- Multi-Window Support
- Play/Editor Mode Separation
- Scene Serialization (JSON)

## Technology Stack

### Core Technologies
- **C++17/20** - Engine core
- **Swift 5** - Editor UI
- **Objective-C++** - Swift-C++ bridge
- **Metal** - Graphics API
- **Metal Shading Language** - GPU shaders

### Third-Party Libraries
- **Jolt Physics** - Physics simulation
- **Assimp** - 3D model import
- **nlohmann/json** - JSON serialization
- **stb_image** - Image loading
- **tinyexr** - EXR/HDR support

## Project Structure

```
CrescentEngine/
├── CrescentEngine/
│   ├── Bridge/              # Swift-C++ bridge
│   ├── Engine/              # Core engine (C++)
│   │   ├── Core/            # Engine, Selection, Gizmo, Time
│   │   ├── ECS/             # Entity, Component, Transform
│   │   ├── Scene/           # Scene, SceneManager, Serialization
│   │   ├── Renderer/        # Renderer, Lighting, Shadows
│   │   ├── Rendering/       # Mesh, Material, Texture
│   │   ├── Components/      # Camera, Light, MeshRenderer, etc.
│   │   ├── Physics/          # PhysicsWorld, Jolt integration
│   │   ├── Animation/       # Skeleton, AnimationClip, Pose
│   │   ├── Assets/          # AssetDatabase
│   │   ├── IBL/             # IBLGenerator
│   │   ├── Input/           # InputManager
│   │   └── Project/         # Project management
│   ├── Shaders/             # Metal shaders
│   └── UI/                  # SwiftUI editor
├── ThirdParty/              # External dependencies
└── scripts/                 # Build scripts
```

## Performance

### Target Performance (1920x1080)
- **Frame Time:** 16-27ms (60 FPS target)
- **Shadow Pass:** ~2-5ms
- **Main Pass:** ~8-15ms
- **Post-Processing:** ~3-5ms

### GPU Memory (1920x1080)
- **Shadow Atlas:** 64 MB
- **Render Buffers:** ~48 MB
- **Post-Process Buffers:** ~32 MB
- **Total (buffers only):** ~144 MB

## License

MIT License. See `LICENSE` file for details.

## Acknowledgments

- **Jolt Physics** - Physics engine
- **Assimp** - 3D model import
- **nlohmann/json** - JSON library
- **stb_image** - Image loading
- **tinyexr** - EXR support

See `THIRD_PARTY_NOTICES.md` for full attributions.

## Screenshots

![Editor View](https://github.com/user-attachments/assets/327431a7-0fce-49bc-b799-7cd71ba66d0c)
![Scene View](https://github.com/user-attachments/assets/f5b25593-c6cc-4106-845d-795bf60e95cf)
![Inspector](https://github.com/user-attachments/assets/93e7e722-b712-44dc-b601-b63545284a53)
![Asset Browser](https://github.com/user-attachments/assets/93e7e722-b712-44dc-b601-b63545284a53)

## Demo Video
[![Video: 5NDLy1gafPQ](https://img.youtube.com/vi/5NDLy1gafPQ/0.jpg)](https://www.youtube.com/watch?v=5NDLy1gafPQ)
[![Video: PbiNGClqRY8](https://img.youtube.com/vi/PbiNGClqRY8/0.jpg)](https://www.youtube.com/watch?v=PbiNGClqRY8)

---

**Built for macOS**
