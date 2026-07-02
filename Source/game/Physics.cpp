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
    edges.clear();
    nextBodyId = 0;
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

void PhysicsEngine::computeEdges (const TrackGraph& track, size_t index)
{
    const auto& b = bodies[index];
    auto& e = edges[index];

    auto f = track.advance ({ b.segment, b.distance }, b.dir, b.radiusFwd);
    e.front = { f.pos.segment, f.pos.distance, f.dir };

    auto r = track.advance ({ b.segment, b.distance }, -b.dir, b.radiusBack);
    e.back = { r.pos.segment, r.pos.distance, -r.dir };
}

PhysicsEngine::ScanHit PhysicsEngine::scanAhead (const TrackGraph& track, size_t selfIndex,
                                                 const Edge& from, int outDir, float maxRange) const
{
    ScanHit best;

    int seg = from.segment;
    float coord = from.distance;
    int wdir = outDir;
    float acc = 0.0f;
    bool firstSpan = true;

    for (int hop = 0; hop < 64; ++hop)
    {
        const auto& s = track.getSegment (seg);
        float exitCoord = (wdir > 0) ? s.length : 0.0f;
        float spanLen = (exitCoord - coord) * (float) wdir;

        for (size_t k = 0; k < bodies.size(); ++k)
        {
            if (k == selfIndex || ! bodies[k].active) continue;

            for (const Edge* m : { &edges[k].front, &edges[k].back })
            {
                if (m->segment != seg) continue;

                float rel = (m->distance - coord) * (float) wdir;
                // On the first span allow a small window behind the scan origin
                // so shallow overlaps are seen; anything deeper is a body that
                // sits behind us, not an obstruction ahead.
                if (rel < (firstSpan ? -kOverlapWindow : -kContactEps)) continue;

                float g = acc + rel;
                if (g > maxRange || g >= best.gap) continue;

                best.gap = g;
                best.bodyIndex = (int) k;
                best.buffer = false;
                best.walkDir = wdir;
                best.markerVelDir = m->velDir;
            }
        }

        acc += spanLen;
        if (acc > maxRange)
            break;

        int exitNode = (wdir > 0) ? s.nodeB : s.nodeA;
        int next = track.routeThrough (seg, exitNode);
        if (next < 0)
        {
            if (acc < best.gap)
            {
                best = {};
                best.gap = acc;
                best.buffer = true;
                best.walkDir = wdir;
            }
            break;
        }

        const auto& ns = track.getSegment (next);
        wdir = (ns.nodeA == exitNode) ? 1 : -1;
        coord = (wdir > 0) ? 0.0f : ns.length;
        seg = next;
        firstSpan = false;
    }

    return best;
}

// True if the body cannot move at all in the given direction (relative to its
// facing): it is pinned against a buffer, or against a chain of stationary
// bodies that eventually ends at one
bool PhysicsEngine::isBlockedAhead (const TrackGraph& track, size_t index,
                                    int alongFacing, int depth) const
{
    if (depth > 100)
        return true;

    const Edge& lead = (alongFacing > 0) ? edges[index].front : edges[index].back;
    int outDir = (alongFacing > 0) ? lead.velDir : -lead.velDir;

    auto hit = scanAhead (track, index, lead, outDir, kBlockGap);

    if (hit.buffer)
        return true;
    if (hit.bodyIndex < 0 || hit.gap > kBlockGap)
        return false;

    const auto& o = bodies[(size_t) hit.bodyIndex];
    float oAlong = o.speed * (float) (hit.markerVelDir * hit.walkDir);
    if (std::abs (oAlong) > kContactEps)
        return false;   // it is moving, not a wall

    return isBlockedAhead (track, (size_t) hit.bodyIndex,
                           hit.markerVelDir * hit.walkDir, depth + 1);
}

void PhysicsEngine::step (const TrackGraph& track, float dt)
{
    for (auto& b : bodies)
        b.moved = 0.0f;

    float subDt = dt / (float) kSubSteps;

    for (int sub = 0; sub < kSubSteps; ++sub)
    {
        edges.resize (bodies.size());
        for (size_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].active)
                computeEdges (track, i);

        // 1. Friction: decelerate free-rolling bodies toward zero
        for (auto& b : bodies)
        {
            if (! b.active || b.friction <= 0.0f) continue;

            float dv = b.friction * subDt;
            if (b.speed > dv)        b.speed -= dv;
            else if (b.speed < -dv)  b.speed += dv;
            else                     b.speed = 0.0f;
        }

        // 2. Move, clamping BEFORE movement so nothing can tunnel
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            auto& b = bodies[i];
            if (! b.active) continue;

            b.speed = std::max (-kMaxSpeed, std::min (kMaxSpeed, b.speed));

            float desired = std::abs (b.speed) * subDt;
            if (desired <= 0.0f) continue;

            const Edge& lead = (b.speed > 0.0f) ? edges[i].front : edges[i].back;
            int outDir = (b.speed > 0.0f) ? lead.velDir : -lead.velDir;

            auto hit = scanAhead (track, i, lead, outDir, desired + kContactEps);
            float allowed = std::min (desired, std::max (0.0f, hit.gap));

            if (allowed > 0.0f)
            {
                int moveDir = (b.speed > 0.0f) ? b.dir : -b.dir;
                auto res = track.advance ({ b.segment, b.distance }, moveDir, allowed);
                b.segment = res.pos.segment;
                b.distance = res.pos.distance;
                b.dir = (b.speed > 0.0f) ? res.dir : -res.dir;
                b.moved += allowed;
                if (res.stopped)
                    b.speed = 0.0f;
                computeEdges (track, i);
            }

            if (hit.bodyIndex >= 0)
            {
                // Contact this substep: inelastic momentum transfer,
                // but only when approaching, never when separating
                auto& o = bodies[(size_t) hit.bodyIndex];
                float along = (float) (hit.markerVelDir * hit.walkDir);
                float va = std::abs (b.speed);
                float vo = o.speed * along;

                if (b.speed != 0.0f && va - vo > kContactEps)
                {
                    if (std::abs (vo) <= kContactEps
                        && isBlockedAhead (track, (size_t) hit.bodyIndex,
                                           hit.markerVelDir * hit.walkDir, 0))
                    {
                        // Pushing an immovable chain: fully blocked, no bouncing
                        b.speed = 0.0f;
                    }
                    else
                    {
                        float w = (b.mass * va + o.mass * vo) / (b.mass + o.mass);
                        b.speed = (b.speed > 0.0f) ? w : -w;
                        o.speed = w * along;
                    }
                }
            }
            else if (hit.buffer)
            {
                // Fully blocked against the end of the track
                b.speed = 0.0f;
            }
        }

        // 3. Gentle separation for residual overlaps (capped so nothing launches)
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            auto& b = bodies[i];
            if (! b.active) continue;

            auto resolve = [&] (const Edge& e, int outDir, int retreatDir)
            {
                auto hit = scanAhead (track, i, e, outDir, 0.0f);
                if (hit.bodyIndex < 0 || hit.gap >= 0.0f) return;

                float push = std::min (kSeparationCap, -hit.gap * 0.5f);
                auto res = track.advance ({ b.segment, b.distance }, retreatDir, push);
                b.segment = res.pos.segment;
                b.distance = res.pos.distance;
                b.dir = (retreatDir == b.dir) ? res.dir : -res.dir;
                computeEdges (track, i);
            };

            resolve (edges[i].front, edges[i].front.velDir, -b.dir);
            resolve (edges[i].back, -edges[i].back.velDir, b.dir);
        }
    }

    bodies.erase (std::remove_if (bodies.begin(), bodies.end(),
        [] (const PhysBody& b) { return ! b.active; }), bodies.end());
    edges.resize (bodies.size());
}

} // namespace game
