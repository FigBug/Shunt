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

int PhysicsEngine::addBody (int segment, float distance, int dir, float mass, float friction,
                            bool isEngine)
{
    PhysBody b;
    b.id = nextBodyId++;
    b.segment = segment;
    b.distance = distance;
    b.dir = dir;
    b.mass = mass;
    b.friction = friction;
    b.isEngine = isEngine;
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

    auto sharedNode = [&] (int s1, int s2) -> int
    {
        const auto& a = track.getSegment (s1);
        const auto& b = track.getSegment (s2);
        if (a.nodeA == b.nodeA || a.nodeA == b.nodeB) return a.nodeA;
        if (a.nodeB == b.nodeA || a.nodeB == b.nodeB) return a.nodeB;
        return -1;
    };
    auto incidentCount = [&] (int node) -> int
    {
        if (node < 0) return 0;
        int c = 0;
        for (int i = 0; i < track.numSegments(); ++i)
        {
            const auto& s = track.getSegment (i);
            if (s.nodeA == node || s.nodeB == node) ++c;
        }
        return c;
    };

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

                // If this body's centre is on a different leg, its surface has
                // straddled onto our line. When the join is a junction (>=3
                // legs) it is fouling from a diverging route, so block it
                // instead of shoving it along a single axis.
                best.foul = (bodies[k].segment != seg)
                            && incidentCount (sharedNode (bodies[k].segment, seg)) >= 3;
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

        // Foul check: a junction (switch/crossing) admits one vehicle at a time.
        // If another body's own extent reaches this node from a *different* leg
        // (not the one we came from, not the routed one), it occupies the
        // junction, so we must stop at the node rather than cross into it. This
        // is what stops vehicles sliding through each other at points and
        // diamond crossings — where the routed path never scans the other legs.
        for (size_t k = 0; k < bodies.size(); ++k)
        {
            if (k == selfIndex || ! bodies[k].active) continue;
            const auto& ob = bodies[k];
            if (ob.segment == seg || ob.segment == next) continue;   // same / routed leg

            const auto& os = track.getSegment (ob.segment);
            float centreToNode, radiusToNode;
            if (os.nodeA == exitNode)
            {
                centreToNode = ob.distance;
                radiusToNode = (ob.dir > 0) ? ob.radiusBack : ob.radiusFwd;
            }
            else if (os.nodeB == exitNode)
            {
                centreToNode = os.length - ob.distance;
                radiusToNode = (ob.dir > 0) ? ob.radiusFwd : ob.radiusBack;
            }
            else continue;   // not incident to this node

            if (centreToNode >= radiusToNode + kContactEps) continue;   // stops short of the node

            if (acc < best.gap)   // block our surface at the node
            {
                best.gap = acc;
                best.bodyIndex = (int) k;
                best.foul = true;
                best.buffer = false;
                best.walkDir = wdir;
                best.markerVelDir = 0;
            }
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

    if (hit.buffer || hit.foul)
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
    contacts.clear();

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

            if (hit.foul)
            {
                // Fouling another vehicle at a shared junction: the geometry is
                // not colinear, so just stop dead at the node rather than
                // transferring momentum along a single axis.
                if (std::abs (b.speed) > kContactEps)
                    contacts.push_back ({ b.segment, b.distance, std::abs (b.speed) });
                b.speed = 0.0f;
            }
            else if (hit.bodyIndex >= 0)
            {
                // Contact this substep: inelastic momentum transfer,
                // but only when approaching, never when separating
                auto& o = bodies[(size_t) hit.bodyIndex];
                float along = (float) (hit.markerVelDir * hit.walkDir);
                float va = std::abs (b.speed);
                float vo = o.speed * along;

                if (b.speed != 0.0f && va - vo > kContactEps)
                {
                    contacts.push_back ({ b.segment, b.distance, va - vo });

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
                if (std::abs (b.speed) > kContactEps)
                    contacts.push_back ({ b.segment, b.distance, std::abs (b.speed) });
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

        // 4. Same-kind overlap. Two engines (which never couple) or two free
        // cars must never share a spot. The edge-scan above misses the case
        // where bodies stack at a junction with their extents pointing the same
        // way, so neither scans toward the other's centre. Push apart any two
        // same-kind bodies whose centres sit on the same segment closer than a
        // car-length. Engine-vs-car pairs are skipped so this never fights
        // coupling (which pulls a free car right up to the engine).
        constexpr float kOverlapGap = 0.8f;
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            if (! bodies[i].active) continue;
            for (size_t j = i + 1; j < bodies.size(); ++j)
            {
                if (! bodies[j].active) continue;
                if (bodies[i].isEngine != bodies[j].isEngine) continue;   // coupling pair
                if (bodies[i].segment != bodies[j].segment) continue;

                float gap = bodies[j].distance - bodies[i].distance;
                if (std::abs (gap) >= kOverlapGap) continue;

                float push = std::min (0.15f, (kOverlapGap - std::abs (gap)) * 0.5f);
                int iDir = (gap >= 0.0f) ? -1 : 1;   // i retreats away from j (ties: i down, j up)

                auto ri = track.advance ({ bodies[i].segment, bodies[i].distance }, iDir, push);
                bodies[i].segment = ri.pos.segment;
                bodies[i].distance = ri.pos.distance;
                auto rj = track.advance ({ bodies[j].segment, bodies[j].distance }, -iDir, push);
                bodies[j].segment = rj.pos.segment;
                bodies[j].distance = rj.pos.distance;
                computeEdges (track, i);
                computeEdges (track, j);
            }
        }

        // 5. Junction cross-leg overlap. The forward scan only follows the routed
        // leg through a switch, so two bodies on *diverging* legs of the same
        // node — which physically converge at that node — are never compared and
        // can slide over each other. For every pair on segments sharing a node
        // that don't route to one another, push each back down its own leg until
        // their surfaces clear the node.
        auto surfaceToNode = [&] (const PhysBody& b, int node, int& awayDir) -> float
        {
            const auto& s = track.getSegment (b.segment);
            if (s.nodeA == node)
            {
                awayDir = 1;   // increasing distance moves away from nodeA
                return b.distance - ((b.dir > 0) ? b.radiusBack : b.radiusFwd);
            }
            if (s.nodeB == node)
            {
                awayDir = -1;
                return (s.length - b.distance) - ((b.dir > 0) ? b.radiusFwd : b.radiusBack);
            }
            awayDir = 0;
            return 1.0e9f;
        };

        for (size_t i = 0; i < bodies.size(); ++i)
        {
            if (! bodies[i].active) continue;
            for (size_t j = i + 1; j < bodies.size(); ++j)
            {
                if (! bodies[j].active) continue;
                if (bodies[i].segment == bodies[j].segment) continue;   // handled by step 4

                const auto& si = track.getSegment (bodies[i].segment);
                const auto& sj = track.getSegment (bodies[j].segment);
                int node = -1;
                if      (si.nodeA == sj.nodeA || si.nodeA == sj.nodeB) node = si.nodeA;
                else if (si.nodeB == sj.nodeA || si.nodeB == sj.nodeB) node = si.nodeB;
                if (node < 0) continue;

                // On the same continuous route (stem<->routed leg, crossing pair,
                // or a plain through-node)? Then the normal scan and coupling
                // already handle them — only separate genuinely diverging legs.
                if (track.routeThrough (bodies[i].segment, node) == (int) bodies[j].segment
                 || track.routeThrough (bodies[j].segment, node) == (int) bodies[i].segment)
                    continue;

                int awayI = 0, awayJ = 0;
                float surfI = surfaceToNode (bodies[i], node, awayI);
                float surfJ = surfaceToNode (bodies[j], node, awayJ);
                float overlap = -(surfI + surfJ);
                if (overlap <= 0.0f || awayI == 0 || awayJ == 0) continue;

                // Push the free car off a fouled junction rather than nudging the
                // driven engine; between like bodies, split the push.
                bool iEng = bodies[i].isEngine, jEng = bodies[j].isEngine;
                float pI = (iEng && ! jEng) ? 0.0f : (jEng && ! iEng) ? overlap : overlap * 0.5f;
                float pJ = (jEng && ! iEng) ? 0.0f : (iEng && ! jEng) ? overlap : overlap * 0.5f;
                pI = std::min (pI, 0.15f);
                pJ = std::min (pJ, 0.15f);

                if (pI > 0.0f)
                {
                    auto r = track.advance ({ bodies[i].segment, bodies[i].distance }, awayI, pI);
                    bodies[i].segment = r.pos.segment; bodies[i].distance = r.pos.distance;
                    computeEdges (track, i);
                }
                if (pJ > 0.0f)
                {
                    auto r = track.advance ({ bodies[j].segment, bodies[j].distance }, awayJ, pJ);
                    bodies[j].segment = r.pos.segment; bodies[j].distance = r.pos.distance;
                    computeEdges (track, j);
                }
            }
        }
    }

    bodies.erase (std::remove_if (bodies.begin(), bodies.end(),
        [] (const PhysBody& b) { return ! b.active; }), bodies.end());
    edges.resize (bodies.size());
}

} // namespace game
