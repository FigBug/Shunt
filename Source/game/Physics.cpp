#include "Physics.h"
#ifndef PHYSICS_TEST_MODE
#include "Track.h"
#endif
#include <cmath>
#include <algorithm>

namespace game
{

void PhysicsEngine::clear()
{
    bodies.clear();
    joints.clear();
    nextBodyId = 0;
    nextJointId = 0;
}

int PhysicsEngine::addBody (int segment, float distance, int dir, float mass, float friction)
{
    PhysBody b;
    b.id = nextBodyId++;
    b.segment = segment;
    b.distance = distance;
    b.dir = dir;
    b.mass = mass;
    b.friction = friction;
    bodies.push_back (b);
    return b.id;
}

void PhysicsEngine::removeBody (int id)
{
    removeJointsForBody (id);
    for (auto& b : bodies)
        if (b.id == id) b.active = false;
}

PhysBody* PhysicsEngine::findBody (int id)
{
    for (auto& b : bodies) if (b.id == id && b.active) return &b;
    return nullptr;
}

const PhysBody* PhysicsEngine::findBody (int id) const
{
    for (const auto& b : bodies) if (b.id == id && b.active) return &b;
    return nullptr;
}

int PhysicsEngine::addJoint (int bodyA, int bodyB, float restLength)
{
    PhysJoint j;
    j.id = nextJointId++;
    j.bodyA = bodyA;
    j.bodyB = bodyB;
    j.restLength = restLength;
    joints.push_back (j);
    return j.id;
}

void PhysicsEngine::removeJoint (int id)
{
    joints.erase (std::remove_if (joints.begin(), joints.end(),
        [id] (const PhysJoint& j) { return j.id == id; }), joints.end());
}

void PhysicsEngine::removeJointsForBody (int bodyId)
{
    joints.erase (std::remove_if (joints.begin(), joints.end(),
        [bodyId] (const PhysJoint& j) { return j.bodyA == bodyId || j.bodyB == bodyId; }),
        joints.end());
}

float PhysicsEngine::signedTrackDist (const TrackGraph& track,
                                       const PhysBody& from, const PhysBody& to) const
{
    if (from.segment == to.segment)
        return to.distance - from.distance;

    const auto& segF = track.getSegment (from.segment);
    const auto& segT = track.getSegment (to.segment);

    float best = 1e9f;
    bool found = false;

    for (int sn : { segF.nodeA, segF.nodeB })
    {
        if (sn != segT.nodeA && sn != segT.nodeB) continue;

        if (const auto* sw = track.findSwitch (sn))
        {
            bool ok = false;
            if (from.segment == sw->stemSegment)
                ok = (to.segment == sw->normalSegment || to.segment == sw->reverseSegment);
            else if (from.segment == sw->normalSegment || from.segment == sw->reverseSegment)
                ok = (to.segment == sw->stemSegment);
            if (! ok) continue;
        }

        float dF = (sn == segF.nodeB) ? (segF.length - from.distance) : -from.distance;
        float dT = (sn == segT.nodeA) ? to.distance : -(segT.length - to.distance);
        float total = dF + dT;

        if (! found || std::abs (total) < std::abs (best))
        {
            best = total;
            found = true;
        }
    }

    return found ? best : 1e9f;
}

void PhysicsEngine::step (const TrackGraph& track, float dt)
{
    constexpr int kSubSteps = 4;
    float subDt = dt / (float) kSubSteps;

    for (int sub = 0; sub < kSubSteps; ++sub)
    {

    // 1. Clear forces
    for (auto& b : bodies)
    {
        if (! b.active) continue;
        b.force = 0.0f;
    }

    // 2. Apply friction: decelerate directly (not as a force, to avoid overshoot)
    for (auto& b : bodies)
    {
        if (! b.active || b.friction <= 0.0f) continue;
        if (std::abs (b.velocity) > 0.01f)
        {
            float dv = b.friction * subDt;
            if (b.velocity > 0.0f)
                b.velocity = std::max (0.0f, b.velocity - dv);
            else
                b.velocity = std::min (0.0f, b.velocity + dv);
        }
        else
        {
            b.velocity = 0.0f;
        }
    }

    // 3. Joint spring-damper forces
    for (auto& j : joints)
    {
        if (! j.active) continue;
        auto* a = findBody (j.bodyA);
        auto* b = findBody (j.bodyB);
        if (! a || ! b) continue;

        float dist = signedTrackDist (track, *a, *b);
        if (std::abs (dist) > 1e8f) continue;

        float stretch = std::abs (dist) - j.restLength;
        float sign = (dist > 0.0f) ? 1.0f : -1.0f;
        float relVel = b->velocity - a->velocity;

        float springForce = j.stiffness * stretch * sign;
        float dampForce = j.damping * relVel * sign;
        float totalForce = std::max (-kMaxForce, std::min (kMaxForce, springForce + dampForce));

        a->force += totalForce;
        b->force -= totalForce;
    }

    // 4. Collision: impulse-based (not spring) — hard inelastic
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        if (! bodies[i].active) continue;

        for (size_t k = i + 1; k < bodies.size(); ++k)
        {
            if (! bodies[k].active) continue;

            bool joined = false;
            for (const auto& j : joints)
                if (j.active && ((j.bodyA == bodies[i].id && j.bodyB == bodies[k].id) ||
                                  (j.bodyA == bodies[k].id && j.bodyB == bodies[i].id)))
                    { joined = true; break; }
            if (joined) continue;

            float dist = signedTrackDist (track, bodies[i], bodies[k]);
            if (std::abs (dist) > 1e8f) continue;

            float absDist = std::abs (dist);
            if (absDist >= kMinGap) continue;

            float sign = (dist > 0.0f) ? 1.0f : -1.0f;

            // Check if approaching
            float vi = bodies[i].velocity;
            float vk = bodies[k].velocity;
            float relApproach = (vi - vk) * sign;

            if (relApproach > 0.0f)
            {
                // Inelastic collision: share velocity by mass
                float totalMass = bodies[i].mass + bodies[k].mass;
                float newV = (bodies[i].mass * vi + bodies[k].mass * vk) / totalMass;
                bodies[i].velocity = newV;
                bodies[k].velocity = newV;
            }

            // Separate: push apart along track (stay on segment)
            float penetration = kMinGap - absDist;
            if (penetration > 0.01f)
            {
                float totalMass = bodies[i].mass + bodies[k].mass;
                float sepI = penetration * (bodies[k].mass / totalMass) * 0.5f;
                float sepK = penetration * (bodies[i].mass / totalMass) * 0.5f;

                auto nudge = [&] (PhysBody& body, float amount)
                {
                    const auto& seg = track.getSegment (body.segment);
                    body.distance = std::max (0.0f, std::min (seg.length, body.distance + amount));
                };

                nudge (bodies[i], -sign * sepI);
                nudge (bodies[k], sign * sepK);
            }
        }
    }

    // 5. Integrate: F = ma → a = F/m → v += a*dt → pos += v*dt
    for (auto& b : bodies)
    {
        if (! b.active) continue;

        float accel = b.force / b.mass;
        b.velocity += accel * subDt;

        // Clamp velocity
        b.velocity = std::max (-30.0f, std::min (30.0f, b.velocity));

        if (std::abs (b.velocity) < 0.001f)
        {
            b.velocity = 0.0f;
            continue;
        }

        float moveDist = b.velocity * subDt;
        int moveDir = (moveDist > 0.0f) ? b.dir : -b.dir;
        TrackPos pos { b.segment, b.distance };
        auto result = track.advance (pos, moveDir, std::abs (moveDist));
        b.segment = result.pos.segment;
        b.distance = result.pos.distance;
        if (moveDist > 0.0f)
            b.dir = result.dir;
        else
            b.dir = -result.dir;

        if (result.stopped)
            b.velocity = 0.0f;
    }

    } // end substep loop

    // Cleanup
    bodies.erase (std::remove_if (bodies.begin(), bodies.end(),
        [] (const PhysBody& b) { return ! b.active; }), bodies.end());
}

} // namespace game
