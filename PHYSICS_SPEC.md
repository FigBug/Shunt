# Physics Spec for Shunt

## The Problem

We have a 1D track graph. Vehicles (engines and cars) move along the track.
The current physics is a mess of edge cases. This spec defines exactly what
needs to happen, then we rebuild from scratch.

## Entities

### Engine
- Has a position on the track (segment + distance)
- Has a direction (which way is "forward")
- Has a speed (set by player throttle or AI)
- Has coupled cars (front list and rear list)
- Coupled cars are NOT independent physics objects — they are rigidly attached
  to the engine at fixed offsets. They move when the engine moves. Period.

### Free Car
- Has a position on the track
- Has a direction
- Has a speed (can coast, decelerates via friction)
- Is an independent physics object

## Rules

### Movement
1. Each frame, the engine's desired speed is set by throttle input
2. Each frame, free cars decelerate toward 0 via friction
3. All vehicles advance along the track by speed * dt
4. If a vehicle reaches a buffer (track end), it stops. Speed = 0.

### Collision
5. Vehicles CANNOT pass through each other
6. An engine's collision boundary extends to its outermost coupled car
7. When a fast vehicle catches a slow vehicle, the fast one is clamped
8. On contact, momentum transfers: v = (m1*v1 + m2*v2)/(m1+m2)
9. A chain of cars against a buffer: all speeds become 0. No bouncing.

### Coupling
10. Engine approaches a free car of the right colour → car attaches
11. Car goes on front or rear list based on which side it was approached from
12. Coupled cars lose their physics body immediately

### Decoupling
13. All cars detach at once, placed at their current positions
14. Each car gets a new physics body with the engine's current speed projected
    onto their track direction
15. Recouple lock prevents immediate re-attachment

## Architecture

### What the physics engine does
- Owns a list of PhysBody (one per engine, one per free car)
- Each body: segment, distance, dir, speed, mass, friction
- Each frame: apply friction, advance positions, clamp against buffers,
  detect collisions, resolve with momentum transfer + separation

### What the physics engine does NOT do
- It does not know about coupled cars
- It does not rebuild bodies every frame
- Bodies are persistent — created on spawn, removed on couple/despawn

### What GameState does
- Creates engine physics bodies on spawn
- Creates free car physics bodies on spawn / decouple
- Each frame: sets engine body speed from throttle, steps physics, reads back
- Coupling: checks if free car is near engine's leading edge → couple, remove body
- Leading edge = engine position + frontCars.size() * spacing (or rear)
- Decoupling: creates new free car bodies with projected speed

### Collision boundary for engines
- The engine body's position is at the ENGINE, not the leading car
- When checking collision, the effective position of the engine extends
  by (frontCars * spacing) ahead and (rearCars * spacing) behind
- This is handled by a radius field on the physics body

## What went wrong before
1. Rebuilding all physics bodies every frame caused position drift and phantom overlaps
2. Coupled cars as immovable physics bodies created infinite energy on collision
3. Coupled cars as movable bodies drifted away from the engine
4. World-space collision detection failed on parallel tracks
5. Separation pushback launched vehicles backward
6. Momentum transfer created energy from nothing when chains hit buffers

## The fix
- Persistent bodies, no rebuild
- No physics bodies for coupled cars — radius handles it
- 1D track-space collision only (same segment + adjacent)
- Clamping BEFORE movement prevents tunneling
- Speed zeroed when fully blocked
- Gentle separation for overlaps (cap at 0.1 per iteration)
- Momentum transfer only when approaching, not when separating
