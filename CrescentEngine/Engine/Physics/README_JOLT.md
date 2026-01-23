# Crescent Engine - Jolt Physics Integration Guide


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

### Why we do this (current design)

- Keep the integration simple and stable by relying on Jolt's default allocator.
- Avoid coupling physics memory to the engine's global allocator while the integration
  is still evolving.
- Use Jolt's `TempAllocatorImpl` for short-lived allocations during update, which is
  the intended fast path in Jolt's design.

### Where the hooks live (exact code)

All allocator setup happens here:

- `CrescentEngine/Engine/Physics/PhysicsWorld.cpp`
- `PhysicsWorld::initialize()`

Current setup (summary):

- `JPH::RegisterDefaultAllocator();`
- `m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);`

### How to replace with a custom allocator (hook point)

Replace the default allocator registration with your own registration or hooks.
The **only place** you need to change is the `PhysicsWorld::initialize()` block.

Recommended pattern:

1) Keep the one-time initialization guard (`g_JoltInitialized`, `g_JoltRefCount`).
2) Swap the allocator registration call.
3) Keep the shutdown symmetry in `PhysicsWorld::shutdown()`.

Pseudo-layout (conceptual):

```
if (!g_JoltInitialized) {
    // replace this
    JPH::RegisterDefaultAllocator();

    // with your own allocator hook / registration
    // (exact function depends on your Jolt build options)

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    g_JoltInitialized = true;
}
```

Notes:
- We intentionally keep allocator setup **process-global** (not per scene).
- The temp allocator is per `PhysicsWorld` instance and used for the update loop.
- If you replace the allocator, ensure it is thread-safe (Jolt runs jobs in a pool).

### Temp Allocator (per-world scratch memory)

- Current size: **10 MB**
- Used in: `physicsSystem.Update(fixedStep, ..., tempAllocator, jobSystem)`
- Safe to tune based on scene complexity and expected peak allocation.

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

## Collision Layers & Broadphase Filters

Crescent currently uses a **two-layer** broadphase setup:

- `NonMoving` (static bodies)
- `Moving` (dynamic and kinematic bodies)

These are defined in `PhysicsWorld.cpp` under the `Layers` namespace.

Broadphase filters:

- `ObjectLayerPairFilterImpl` blocks Static vs Static contacts.
- `ObjectVsBroadPhaseLayerFilterImpl` only allows static bodies to collide with moving.

This keeps the broadphase fast and is enough for the current ECS needs.

## Custom Contact Listener (How It Works)

Crescent uses a **custom Jolt contact listener** implemented as an inner class:

- `PhysicsWorld::PhysicsWorldImpl::ContactListenerImpl`
- File: `CrescentEngine/Engine/Physics/PhysicsWorld.cpp`

The listener is designed for two goals:

1) Perform **layer/mask validation** before Jolt accepts contacts.
2) Convert Jolt contact callbacks into **thread-safe ECS events**.

### Contact Validation (Layer/Mask Filtering)

`OnContactValidate(...)` does filtering based on Crescent's
`PhysicsCollider` layer and mask:

- Each collider has `collisionLayer` and `collisionMask`
- The listener checks layer/mask compatibility before allowing contact

This is the early gate before narrow-phase contacts are accepted.

Implementation notes:

- We store the collider layer in Jolt's collision group **GroupID**.
- We store the collider mask in **SubGroupID**.
- Validation computes bitmasks and rejects contact if either side masks it out.

### Contact Events (Enter/Stay/Exit)

`ContactListenerImpl` records contact events into a thread-safe queue:

- `OnContactAdded` -> Enter
- `OnContactPersisted` -> Stay
- `OnContactRemoved` -> Exit

Events are stored in `pendingEvents`, and an `activeContacts` table is used
so Exit events can be sent correctly.

We also de-duplicate events per frame:

- In `dispatchContactEvents()` we use a `(BodyPairKey + EventType)` set
  so duplicate calls in the same frame are coalesced.

Captured contact payload:

- World-space normal and penetration depth
- Contact points (if available from the manifold)

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

### Entity Mapping (Body -> ECS)

We store a pointer to the owning `Entity` in Jolt user data:

- `settings.mUserData = reinterpret_cast<uint64_t>(entity);`

When dispatching, we resolve it back:

- `reinterpret_cast<Entity*>(body.GetUserData())`

This is how contact events are routed to ECS components.

### Threading Model (Important)

Jolt contact callbacks may run on a physics worker thread. We do **not** touch ECS
from those callbacks. Instead:

1) Collect `ContactEvent` in a mutex-protected vector
2) Dispatch later on the main update thread via `dispatchContactEvents()`

This keeps physics thread-safe and avoids ECS mutations from physics threads.

### Contact Material Combine Rules

Jolt asks for combined material properties when two bodies touch. Crescent
implements two callbacks:

- `CombineFriction(...)`
- `CombineRestitution(...)`

Behavior:

- Read friction/restitution from both colliders.
- Resolve a combine mode from both sides (Min/Max/Multiply/Average).
- Apply the combined value deterministically.

Hooked in `PhysicsWorld::initialize()`:

- `physicsSystem.SetCombineFriction(&CombineFriction);`
- `physicsSystem.SetCombineRestitution(&CombineRestitution);`

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

Internally:

- `QueryBodyFilter` checks collision masks and trigger inclusion.
- We also optionally ignore a specific entity (e.g., shooter).

## Debug Draw

`PhysicsWorld::debugDraw()` uses the engine DebugRenderer to draw collider
proxies when debug draw is enabled.

Color coding:

- Static: grey
- Dynamic: cyan
- Kinematic: yellow
- Trigger: orange


