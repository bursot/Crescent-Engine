# CrescentEngine

CrescentEngine is a work-in-progress (experimental actually) real-time engine for macOS built on Metal.

## Features
- Metal renderer
- ECS architecture
- Scene serialization
- Physics via Jolt
- Asset import via Assimp
- JSON via nlohmann/json

## Requirements
- macOS 14+ (Intel or Apple Silicon)
- Xcode 15+ (Swift 5)
- CMake 3.26+ and Git (for dependency builds)

## Quick Start
1. Run dependency setup:
   `./scripts/setup_deps.sh`
2. Open the project:
   `open CrescentEngine.xcodeproj`
3. Build the `CrescentEngine` scheme (Debug or Release).

## Dependency Layout
The setup script populates and builds these paths:
- `ThirdParty/JoltPhysics`
- `ThirdParty/assimp`
- `ThirdParty/jolt-build-debug` and `ThirdParty/jolt-build-release`
- `ThirdParty/assimp-build-debug` and `ThirdParty/assimp-build-release`

If you want different versions, edit `scripts/setup_deps.sh`.

## License
MIT. See `LICENSE`.

## Third-Party Notices
See `THIRD_PARTY_NOTICES.md`.
