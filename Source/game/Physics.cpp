#include "Physics.h"
#ifndef PHYSICS_TEST_MODE
#include "Track.h"
#endif
#include <cmath>
#include <algorithm>

namespace game
{

// ============================================================================
// Body bookkeeping
// ============================================================================

void PhysicsEngine::clear()
{
    bodies.clear();
    colliders.clear();
    contactC.clear();
    contacts.clear();
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

// ============================================================================
// Local geometry helpers
// ============================================================================

namespace
{
    // Distance from `pos` in `dir` to the nearest dead-end (buffer stop) along
    // the routed path, capped at `cap`. Used to stop a leading edge at buffers.
    float distToDeadEnd (const TrackGraph& track, TrackPos pos, int dir, float cap)
    {
        int   seg   = pos.segment;
        int   d     = dir;
        float coord = pos.distance;
        float acc   = 0.0f;

        for (int hop = 0; hop < 64; ++hop)
        {
            const auto& s = track.getSegment (seg);
            float exitCoord = (d > 0) ? s.length : 0.0f;
            float span = (exitCoord - coord) * (float) d;
            if (span < 0.0f) span = 0.0f;

            if (acc + span >= cap)
                return cap;

            int node = (d > 0) ? s.nodeB : s.nodeA;
            int next = track.routeThrough (seg, node);
            if (next < 0)
                return acc + span;   // dead end here

            acc += span;
            const auto& ns = track.getSegment (next);
            d     = (ns.nodeA == node) ? 1 : -1;
            coord = (d > 0) ? 0.0f : ns.length;
            seg   = next;
        }
        return cap;
    }

    struct Obb { float cx, cy, ux, uy, hl, hw; };

    // Separating-axis test. Returns true when the boxes overlap, and fills the
    // minimum-penetration normal (pointing A -> B) and depth.
    bool obbOverlap (const Obb& a, const Obb& b, float& nx, float& ny, float& pen)
    {
        float dx = b.cx - a.cx, dy = b.cy - a.cy;
        float axes[4][2] = {
            {  a.ux,  a.uy }, { -a.uy,  a.ux },
            {  b.ux,  b.uy }, { -b.uy,  b.ux },
        };

        float best = -1.0e9f, bnx = 1.0f, bny = 0.0f;
        for (auto& L : axes)
        {
            float lx = L[0], ly = L[1];
            float pa = a.hl * std::abs (a.ux * lx + a.uy * ly)
                     + a.hw * std::abs (-a.uy * lx + a.ux * ly);
            float pb = b.hl * std::abs (b.ux * lx + b.uy * ly)
                     + b.hw * std::abs (-b.uy * lx + b.ux * ly);
            float dist = dx * lx + dy * ly;
            float sep  = std::abs (dist) - (pa + pb);
            if (sep > 0.0f)
                return false;            // a separating axis exists

            if (sep > best)              // least-overlapping axis = contact normal
            {
                best = sep;
                float s = (dist >= 0.0f) ? 1.0f : -1.0f;
                bnx = lx * s; bny = ly * s;
            }
        }

        nx = bnx; ny = bny; pen = -best;
        return true;
    }
}

// ============================================================================
// Collider generation + contact detection
// ============================================================================

void PhysicsEngine::buildColliders (const TrackGraph& track)
{
    colliders.clear();

    for (size_t i = 0; i < bodies.size(); ++i)
    {
        const auto& b = bodies[i];
        if (! b.active) continue;

        // Tile the body's extent [-radiusBack, +radiusFwd] with vehicle-sized
        // boxes that follow the rail (so a consist bends around curves).
        float start = -(b.radiusBack - kHalfLen);
        float end   =  (b.radiusFwd  - kHalfLen);
        if (end < start) end = start;

        for (float o = start; o <= end + 1.0e-3f; o += 2.0f * kHalfLen)
        {
            TrackPos cp;
            int facingDir;
            if (o >= 0.0f)
            {
                auto r = track.advance ({ b.segment, b.distance }, b.dir, o);
                cp = r.pos; facingDir = r.dir;
            }
            else
            {
                auto r = track.advance ({ b.segment, b.distance }, -b.dir, -o);
                cp = r.pos; facingDir = -r.dir;
            }

            float ang = track.tangentAngle (cp);
            if (facingDir < 0) ang += 3.14159265358979f;

            Collider c;
            c.body = (int) i;
            track.worldXY (cp, c.cx, c.cy);
            c.ux = std::cos (ang);
            c.uy = std::sin (ang);
            c.halfLen = kHalfLen;
            c.halfWid = kHalfWid;
            colliders.push_back (c);
        }
    }
}

void PhysicsEngine::detectContacts (const TrackGraph& track, float dt)
{
    contactC.clear();

    // Body-vs-body: every collider pair from different bodies.
    for (size_t i = 0; i < colliders.size(); ++i)
    {
        const auto& ca = colliders[i];
        Obb oa { ca.cx, ca.cy, ca.ux, ca.uy, ca.halfLen, ca.halfWid };

        for (size_t j = i + 1; j < colliders.size(); ++j)
        {
            const auto& cb = colliders[j];
            if (ca.body == cb.body) continue;

            Obb ob { cb.cx, cb.cy, cb.ux, cb.uy, cb.halfLen, cb.halfWid };
            float nx, ny, pen;
            if (! obbOverlap (oa, ob, nx, ny, pen)) continue;

            ContactC ct;
            ct.bodyA = ca.body; ct.bodyB = cb.body;
            ct.nx = nx; ct.ny = ny;
            ct.jA = ca.ux * nx + ca.uy * ny;      // face . n
            ct.jB = cb.ux * nx + cb.uy * ny;
            ct.pen = pen;
            ct.px = (ca.cx + cb.cx) * 0.5f;
            ct.py = (ca.cy + cb.cy) * 0.5f;
            contactC.push_back (ct);
        }
    }

    // Body-vs-buffer: a leading edge that has reached a dead end. Modelled as a
    // contact against an immovable wall (bodyB = -1) whose normal is the facing
    // direction of travel.
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        const auto& b = bodies[i];
        if (! b.active) continue;

        float faceAng = track.tangentAngle ({ b.segment, b.distance });
        if (b.dir < 0) faceAng += 3.14159265358979f;
        float fx = std::cos (faceAng), fy = std::sin (faceAng);

        float frontGap = distToDeadEnd (track, { b.segment, b.distance }, b.dir, b.radiusFwd + 1.0f);
        if (frontGap <= b.radiusFwd + kSlop)
        {
            ContactC ct;
            ct.bodyA = (int) i; ct.bodyB = -1;
            ct.nx = fx; ct.ny = fy;
            ct.jA = 1.0f;   // n == facing
            ct.pen = std::max (0.0f, b.radiusFwd - frontGap);
            contactC.push_back (ct);
        }

        float backGap = distToDeadEnd (track, { b.segment, b.distance }, -b.dir, b.radiusBack + 1.0f);
        if (backGap <= b.radiusBack + kSlop)
        {
            ContactC ct;
            ct.bodyA = (int) i; ct.bodyB = -1;
            ct.nx = -fx; ct.ny = -fy;
            ct.jA = -1.0f;   // n == -facing
            ct.pen = std::max (0.0f, b.radiusBack - backGap);
            contactC.push_back (ct);
        }
    }
}

// ============================================================================
// Step
// ============================================================================

void PhysicsEngine::step (const TrackGraph& track, float dt)
{
    contacts.clear();
    for (auto& b : bodies)
        b.moved = 0.0f;

    float subDt = dt / (float) kSubSteps;

    for (int sub = 0; sub < kSubSteps; ++sub)
    {
        // 1. Friction: coast free-rolling bodies toward rest.
        for (auto& b : bodies)
        {
            if (! b.active || b.friction <= 0.0f) continue;
            float dv = b.friction * subDt;
            if (b.speed >  dv)      b.speed -= dv;
            else if (b.speed < -dv) b.speed += dv;
            else                    b.speed = 0.0f;
        }

        // 2. Detect contacts at current positions.
        buildColliders (track);
        detectContacts (track, subDt);

        // 3. Velocity solve — sequential impulses, projected onto each body's
        //    single track DOF. Restitution 0: trains do not bounce.
        for (int iter = 0; iter < kSolverIters; ++iter)
        {
            for (auto& ct : contactC)
            {
                auto& A = bodies[(size_t) ct.bodyA];
                float sB = (ct.bodyB >= 0) ? bodies[(size_t) ct.bodyB].speed : 0.0f;
                float vn = A.speed * ct.jA - sB * ct.jB;   // closing speed along n
                if (vn <= kContactEps) continue;           // separating / resting

                float invMassA = 1.0f / A.mass;
                float k = ct.jA * ct.jA * invMassA;
                if (ct.bodyB >= 0)
                    k += ct.jB * ct.jB / bodies[(size_t) ct.bodyB].mass;
                if (k < 1.0e-6f) continue;   // wedge: no DOF relieves it (position pass separates)

                float P = -(1.0f + kRestitution) * vn / k;
                A.speed += ct.jA * P * invMassA;
                if (ct.bodyB >= 0)
                {
                    auto& B = bodies[(size_t) ct.bodyB];
                    B.speed += -ct.jB * P / B.mass;
                }

                if (iter == 0 && vn > 0.5f)   // first pass: report the impact for sound
                    contacts.push_back ({ A.segment, A.distance, vn });
            }
        }

        // 4. Integrate along the track; the leading edge is clamped at buffers.
        for (auto& b : bodies)
        {
            if (! b.active) continue;
            b.speed = std::max (-kMaxSpeed, std::min (kMaxSpeed, b.speed));
            if (std::abs (b.speed) < 1.0e-5f) continue;

            bool  forward = (b.speed > 0.0f);
            int   moveDir = forward ? b.dir : -b.dir;
            float leadR   = forward ? b.radiusFwd : b.radiusBack;
            float want    = std::abs (b.speed) * subDt;

            float gap = distToDeadEnd (track, { b.segment, b.distance }, moveDir, leadR + want + 1.0f);
            float allowed = want;
            if (gap - leadR < want)          // leading edge would cross the buffer
            {
                allowed = std::max (0.0f, gap - leadR);
                b.speed = 0.0f;
            }

            if (allowed > 0.0f)
            {
                auto res = track.advance ({ b.segment, b.distance }, moveDir, allowed);
                b.segment = res.pos.segment;
                b.distance = res.pos.distance;
                b.dir = forward ? res.dir : -res.dir;   // travel dir, not the just-zeroed speed
                b.moved += allowed;
            }
        }

        // 5. Positional correction (NGS): push penetrating pairs apart along
        //    their own tracks so wedges stay visibly wedged and nothing sinks in.
        buildColliders (track);
        detectContacts (track, subDt);
        for (auto& ct : contactC)
        {
            float corr = ct.pen - kSlop;
            if (corr <= 0.0f) continue;

            auto& A = bodies[(size_t) ct.bodyA];
            float invMassA = 1.0f / A.mass;
            float k = ct.jA * ct.jA * invMassA;
            if (ct.bodyB >= 0)
                k += ct.jB * ct.jB / bodies[(size_t) ct.bodyB].mass;
            if (k < 1.0e-6f) continue;

            float lambda = kBaumgarte * corr / k;

            auto slide = [&] (PhysBody& b, float along)
            {
                if (std::abs (along) < 1.0e-5f) return;
                int md = (along > 0.0f) ? b.dir : -b.dir;
                auto res = track.advance ({ b.segment, b.distance }, md, std::abs (along));
                b.segment = res.pos.segment;
                b.distance = res.pos.distance;
            };

            // A retreats along -n (i.e. -jA in its own DOF); B advances along +n.
            slide (A, -ct.jA * lambda * invMassA);
            if (ct.bodyB >= 0)
            {
                auto& B = bodies[(size_t) ct.bodyB];
                slide (B, ct.jB * lambda / B.mass);
            }
        }
    }

    // Drop removed bodies once the step is done (indices stay valid during it).
    bodies.erase (std::remove_if (bodies.begin(), bodies.end(),
        [] (const PhysBody& b) { return ! b.active; }), bodies.end());
}

} // namespace game
