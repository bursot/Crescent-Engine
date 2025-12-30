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

   
<img width="1512" height="982" alt="Ekran Resmi 2026-12-31 02 18 35" src="https://github.com/user-attachments/assets/e6c55517-22ec-4526-b150-52d8b496e645" />
<img width="1512" height="982" alt="Ekran Resmi 2025-12-27 22 51 38" src="https://github.com/user-attachments/assets/de853ff4-b5be-40a7-a69b-238e6db33a35" />
<img width="1512" height="982" alt="Ekran Resmi 2025-12-24 23 51 47" src="https://github.com/user-attachments/assets/e3f88f6a-1312-4bef-8034-5c642a18d867" />
<img width="1512" height="982" alt="Ekran Resmi 2025-12-24 23 49 59" src="https://github.com/user-attachments/assets/fd74e850-f561-4cfc-a5f8-65bf41c66a6a" />
<img width="1512" height="982" alt="Ekran Resmi 2025-12-21 19 56 24" src="https://github.com/user-attachments/assets/0bb6fb9c-66b1-49fe-b8d6-8a1b4d408431" />
<img width="1512" height="982" alt="Ekran Resmi 2025-12-21 19 55 37" src="https://github.com/user-attachments/assets/f9d92491-2f25-4327-8951-76f2498441ee" />
