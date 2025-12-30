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

   

<img width="1510" height="886" alt="531120812-f9d92491-2f25-4327-8951-76f2498441ee" src="https://github.com/user-attachments/assets/327431a7-0fce-49bc-b799-7cd71ba66d0c" />
<img width="1511" height="882" alt="531120820-0bb6fb9c-66b1-49fe-b8d6-8a1b4d408431" src="https://github.com/user-attachments/assets/f5b25593-c6cc-4106-845d-795bf60e95cf" />
<img width="1509" height="892" alt="531120826-e3f88f6a-1312-4bef-8034-5c642a18d867" src="https://github.com/user-attachments/assets/93e7e722-b712-44dc-b601-b63545284a53" />
<img width="1502" height="880" alt="531120828-de853ff4-b5be-40a7-a69b-238e6db33a35" src="https://github.com/user-attachments/assets/e73d618d-ebca-422e-b570-5f561f7a337e" />
<img width="1508" height="885" alt="531120835-e6c55517-22ec-4526-b150-52d8b496e645" src="https://github.com/user-attachments/assets/6a966dd5-6295-4298-86c6-15bf35c8661f" />
