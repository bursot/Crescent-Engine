# Crescent Engine - Jolt Physics Integration Guide

This document describes how Crescent Engine integrates Jolt Physics, with a focus on
memory allocation hooks and the custom contact listener pipeline. 

## Quick Map (Where Things Live)

Core integration:
- Engine physics layer: `CrescentEngine/Engine/Physics/PhysicsWorld.hpp`
- Implementation: `CrescentEngine/Engine/Physics/PhysicsWorld.cpp`

Collider & rigidbody components:
- Collider component: `CrescentEngine/Engine/Components/PhysicsCollider.hpp`
- Rigidbody component: `CrescentEngine/Engine/Components/Rigidbody.hpp`
- Character controller: `CrescentEngine/Engine/Components/CharacterController.hpp`

ECS event callbacks:
- `CrescentEngine/Engine/ECS/Component.hpp`
- `CrescentEngine/Engine/ECS/Entity.cpp`

UI inspector + editor:
- `CrescentEngine/UI/InspectorPanel.swift`
- `CrescentEngine/Bridge/CrescentEngineBridge.mm`

Build/deps:
- `scripts/setup_deps.sh`
- `ThirdParty/JoltPhysics`

## Build and Dependency Setup

1) Jolt is pulled and built by `scripts/setup_deps.sh`.
2) The engine links to Jolt static libs created by that script.
3) Xcode project includes the Jolt include path and `-lJolt`.

## Initialization and Shutdown Flow

Initialization happens once (per process), in `PhysicsWorld::initialize()`:

- `JPH::RegisterDefaultAllocator()`
- `JPH::Factory::sInstance = new JPH::Factory()`
- `JPH::RegisterTypes()`

Then a per-world setup is created:

- `JPH::TempAllocatorImpl` (10 MB scratch allocator)
- `JPH::JobSystemThreadPool`
- `JPH::PhysicsSystem::Init(...)`
- `SetContactListener(...)`
- `SetCombineFriction(...)`, `SetCombineRestitution(...)`
- `SetGravity(...)`

Shutdown reverses that:

- Removes bodies
- Clears contact listener
- `JPH::UnregisterTypes()`
- deletes factory instance

## Memory Allocation Hooks (What We Do Today)

Right now, Crescent uses Jolt's **default allocator**:

- `PhysicsWorld::initialize()` calls `JPH::RegisterDefaultAllocator()`
- A `JPH::TempAllocatorImpl` is used during simulation steps

This means Jolt uses its built-in allocator (unless you replace it).

### How to plug in a custom allocator

If you want custom allocation hooks (e.g., your own allocator or telemetry),
replace the default allocator registration in:

- `CrescentEngine/Engine/Physics/PhysicsWorld.cpp`

The hook point is here:

- `PhysicsWorld::initialize()`

Replace the line:

- `JPH::RegisterDefaultAllocator();`

With your custom allocator registration. The exact method depends on your Jolt
build settings (for example, using JPH custom alloc macros or a custom
allocator implementation). Keep the one-time registration semantics intact
(the code uses `g_JoltInitialized` and `g_JoltRefCount`).

### Temp Allocator Size

The temporary allocator is currently:

- `JPH::TempAllocatorImpl(10 * 1024 * 1024)`

If you need a different size, change that value in `PhysicsWorld::initialize()`.

## Custom Contact Listener (How It Works)

Crescent uses a **custom Jolt contact listener** implemented as an inner class:

- `PhysicsWorld::PhysicsWorldImpl::ContactListenerImpl`
- File: `CrescentEngine/Engine/Physics/PhysicsWorld.cpp`

### Contact Validation (Layer/Mask Filtering)

`OnContactValidate(...)` does filtering based on Crescent's
`PhysicsCollider` layer and mask:

- Each collider has `collisionLayer` and `collisionMask`
- The listener checks layer/mask compatibility before allowing contact

This is the early gate before narrow-phase contacts are accepted.

### Contact Events (Enter/Stay/Exit)

`ContactListenerImpl` records contact events into a thread-safe queue:

- `OnContactAdded` -> Enter
- `OnContactPersisted` -> Stay
- `OnContactRemoved` -> Exit

Events are stored in `pendingEvents`, and an `activeContacts` table is used
so Exit events can be sent correctly.

### Triggers vs Collisions

A contact is treated as a trigger when **either body is a sensor**:

- `isTrigger = body1.IsSensor() || body2.IsSensor()`

### Dispatch to ECS

At the end of each physics update:

- `PhysicsWorld::dispatchContactEvents()`
- Converts Jolt events into `PhysicsContact`
- Calls ECS callbacks on the owning entities

Callbacks delivered to components:

- `OnCollisionEnter / Stay / Exit`
- `OnTriggerEnter / Stay / Exit`

## Physics Update Loop (Fixed Step)

`PhysicsWorld::update(deltaTime, simulate)` runs with an accumulator:

- Adds deltaTime to `m_TimeAccumulator`
- Runs up to 4 fixed steps
- Each step calls `physicsSystem.Update(fixedStep, ...)`
- Then syncs bodies and dispatches contact events

The fixed step is set in `m_FixedTimeStep` (default 1/60).

## Shapes and Body Creation

Supported collider shapes in Crescent:

- Box, Sphere, Capsule, Mesh, Convex

Body creation happens via `PhysicsWorld::rebuildBody(...)` and `buildShape(...)`.
Key behaviors:

- Rigidbody type chooses motion type (Static, Dynamic, Kinematic)
- Capsule height is clamped; if height is too small it becomes a sphere
- Mesh colliders can be built from a MeshRenderer in the entity hierarchy
- Collider center offset uses `RotatedTranslatedShape`

## Queries (Raycast, Casts, Overlaps)

`PhysicsWorld` exposes query helpers:

- Raycast / RaycastAll
- SphereCast / SphereCastAll
- BoxCast / BoxCastAll
- CapsuleCast / CapsuleCastAll
- OverlapSphere / OverlapBox

Query filters use collision masks and can include/exclude triggers.

## Debug Draw

`PhysicsWorld::debugDraw()` uses the engine DebugRenderer to draw collider
proxies when debug draw is enabled.

Color coding:

- Static: grey
- Dynamic: cyan
- Kinematic: yellow
- Trigger: orange


