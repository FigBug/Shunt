// Tests for the kinematic track physics engine (see PHYSICS_SPEC.md)
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

namespace game {
struct TrackNode { float x = 0, y = 0; };
struct TrackSegment { int nodeA=0,nodeB=0; float length=0.0f; };
struct SwitchInfo { int node=0,stemSegment=0,normalSegment=0,reverseSegment=0; bool reversed=false; float cooldown=0.0f; };
struct TrackPos { int segment=0; float distance=0.0f; };

class TrackGraph {
public:
    std::vector<TrackNode> nodes;
    std::vector<TrackSegment> segments;
    std::vector<SwitchInfo> switches;

    int addNode(float x, float y) { nodes.push_back({x,y}); return (int)nodes.size()-1; }
    int addSegment(int a, int b) {
        float len = std::hypot(nodes[b].x-nodes[a].x, nodes[b].y-nodes[a].y);
        segments.push_back({a,b,len}); return (int)segments.size()-1;
    }
    int addSegmentLen(int a, int b, float len) {
        segments.push_back({a,b,len}); return (int)segments.size()-1;
    }
    void addSwitch(int node, int stem, int normal, int reverse) {
        switches.push_back({node,stem,normal,reverse,false,0.0f});
    }
    const TrackSegment& getSegment(int i) const { return segments[i]; }
    int numSegments() const { return (int)segments.size(); }
    const SwitchInfo* findSwitch(int node) const {
        for (auto& sw : switches) if (sw.node == node) return &sw;
        return nullptr;
    }

    int routeThrough(int fromSeg, int atNode) const {
        if (const auto* sw = findSwitch(atNode)) {
            if (fromSeg == sw->stemSegment) return sw->reversed ? sw->reverseSegment : sw->normalSegment;
            if (fromSeg == sw->normalSegment) return sw->stemSegment;
            if (fromSeg == sw->reverseSegment) return sw->stemSegment;
            return -1;
        }
        for (int i = 0; i < (int)segments.size(); i++) {
            if (i == fromSeg) continue;
            if (segments[i].nodeA == atNode || segments[i].nodeB == atNode) return i;
        }
        return -1;
    }

    struct MoveResult { TrackPos pos; int dir=1; bool stopped=false; };
    MoveResult advance(TrackPos pos, int dir, float dist) const {
        if (dist <= 0) return {pos, dir, false};
        auto& seg = segments[pos.segment];
        float nd = pos.distance + (float)dir * dist;
        if (nd >= 0 && nd <= seg.length) return {{pos.segment, nd}, dir, false};
        float ov = (nd > seg.length) ? nd - seg.length : -nd;
        int cn = (nd > seg.length) ? seg.nodeB : seg.nodeA;
        int next = routeThrough(pos.segment, cn);
        if (next < 0) {
            float endD = (nd > seg.length) ? seg.length : 0.0f;
            return {{pos.segment, endD}, dir, true};
        }
        auto& ns = segments[next];
        int newDir = (ns.nodeA == cn) ? 1 : -1;
        float startD = (newDir > 0) ? 0.0f : ns.length;
        return advance({next, startD}, newDir, ov);
    }
};
} // namespace game

#define PHYSICS_TEST_MODE 1
#include "../Source/game/Physics.h"
#include "../Source/game/Physics.cpp"

static int gPass = 0, gFail = 0;
#define TEST(name) static void test_##name()
#define RUN(name) do { printf("  %-55s", #name); int before = gFail; test_##name(); if (gFail == before) { printf(" PASS\n"); gPass++; } } while(0)
#define ASSERT(cond) do { if (!(cond)) { printf(" FAIL line %d: %s\n", __LINE__, #cond); gFail++; return; } } while(0)
#define NEAR(a,b,e) ASSERT(std::abs((a)-(b))<(e))

static game::TrackGraph makeLine() {
    game::TrackGraph t;
    t.addNode(0,0); t.addNode(100,0);
    t.addSegment(0,1);
    return t;
}

// ======================== BASIC MOVEMENT ========================

TEST(single_body_moves) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    p.findBody(id)->speed = 5.0f;
    p.step(t, 1.0f);
    NEAR(p.findBody(id)->distance, 15.0f, 0.01f);
    NEAR(p.findBody(id)->moved, 5.0f, 0.01f);
}

TEST(reverse_moves_backward) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 50.0f, 1, 1.0f, 0.0f);
    p.findBody(id)->speed = -5.0f;
    p.step(t, 1.0f);
    NEAR(p.findBody(id)->distance, 45.0f, 0.01f);
    ASSERT(p.findBody(id)->dir == 1);   // facing is preserved when reversing
}

TEST(friction_stops) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 50.0f, 1, 1.0f, 100.0f);
    p.findBody(id)->speed = 1.0f;
    p.step(t, 1.0f);
    NEAR(p.findBody(id)->speed, 0.0f, 0.01f);
}

TEST(coasting_decelerates) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 10.0f, 1, 1.0f, 2.0f);
    p.findBody(id)->speed = 6.0f;
    for (int i = 0; i < 60; i++) p.step(t, 0.016f);
    ASSERT(p.findBody(id)->speed < 6.0f);
    ASSERT(p.findBody(id)->speed >= 0.0f);
    ASSERT(p.findBody(id)->distance > 10.0f);
}

// ======================== BUFFERS ========================

TEST(buffer_stops) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 95.0f, 1, 1.0f, 0.0f);
    p.findBody(id)->speed = 20.0f;
    for (int i = 0; i < 60; i++) p.step(t, 0.016f);
    // Front surface (radius 0.8) stops at the buffer, speed zeroed
    ASSERT(p.findBody(id)->distance <= 100.0f - 0.8f + 0.01f);
    NEAR(p.findBody(id)->speed, 0.0f, 0.01f);
}

TEST(radius_stops_short_of_buffer) {
    auto t = makeLine(); game::PhysicsEngine p;
    // Engine with 2 coupled cars in front: boundary extends 0.8 + 2*1.6
    int id = p.addBody(0, 80.0f, 1, 5.0f, 0.0f);
    p.findBody(id)->radiusFwd = 0.8f + 2.0f * 1.6f;
    for (int i = 0; i < 300; i++) {
        p.findBody(id)->speed = 10.0f;
        p.step(t, 0.016f);
    }
    NEAR(p.findBody(id)->distance, 100.0f - 4.0f, 0.1f);
    NEAR(p.findBody(id)->speed, 0.0f, 0.01f);
}

TEST(no_tunneling_at_high_speed) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 50.0f, 1, 1.0f, 0.0f);
    p.findBody(id)->speed = 30.0f;
    for (int i = 0; i < 300; i++) p.step(t, 0.1f);   // huge dt
    ASSERT(p.findBody(id)->distance <= 100.0f);
    NEAR(p.findBody(id)->speed, 0.0f, 0.01f);
}

// ======================== COLLISIONS ========================

TEST(collision_no_passthrough) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 50.0f, 1, 1.0f, 0.0f);
    p.findBody(a)->speed = 30.0f;
    for (int i = 0; i < 120; i++) p.step(t, 0.016f);
    ASSERT(p.findBody(a)->distance < p.findBody(b)->distance);
    // Surfaces never interpenetrate: centres stay >= 1.6 apart
    ASSERT(p.findBody(b)->distance - p.findBody(a)->distance >= 1.6f - 0.11f);
}

TEST(momentum_transfer) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 12.0f, 1, 1.0f, 0.0f);
    p.findBody(a)->speed = 6.0f;
    p.step(t, 0.5f);  // enough to reach contact
    // Equal masses, inelastic: both end at ~3
    NEAR(p.findBody(a)->speed, 3.0f, 0.2f);
    NEAR(p.findBody(b)->speed, 3.0f, 0.2f);
}

TEST(heavy_pushes_light) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 5.0f, 0.0f);
    int b = p.addBody(0, 20.0f, 1, 1.0f, 0.0f);
    for (int i = 0; i < 300; i++) {
        p.findBody(a)->speed = 8.0f;   // engine keeps driving
        p.step(t, 0.016f);
    }
    ASSERT(p.findBody(b)->distance > 20.0f);
    ASSERT(p.findBody(a)->distance < p.findBody(b)->distance);
    // Pushed car ends up moving at ~engine speed
    ASSERT(p.findBody(b)->speed > 6.0f);
}

TEST(head_on_collision) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 20.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 80.0f, 1, 1.0f, 0.0f);
    p.findBody(a)->speed = 8.0f;
    p.findBody(b)->speed = -8.0f;
    for (int i = 0; i < 300; i++) p.step(t, 0.016f);
    ASSERT(p.findBody(a)->distance < p.findBody(b)->distance);
    ASSERT(p.findBody(b)->distance - p.findBody(a)->distance >= 1.6f - 0.11f);
    // Equal and opposite: both come to rest, no energy created
    NEAR(p.findBody(a)->speed, 0.0f, 0.5f);
    NEAR(p.findBody(b)->speed, 0.0f, 0.5f);
}

TEST(no_transfer_when_separating) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 11.6f, 1, 1.0f, 0.0f);   // touching
    p.findBody(b)->speed = 5.0f;                   // front one drives away
    p.step(t, 0.5f);
    NEAR(p.findBody(a)->speed, 0.0f, 0.01f);       // rear one unaffected
    ASSERT(p.findBody(b)->distance > 11.6f);
}

// ======================== CHAINS & BUFFERS ========================

TEST(chain_rams_buffer_all_stop) {
    auto t = makeLine(); game::PhysicsEngine p;
    // Engine with 2-car front radius pushes 4 free cars into the buffer
    int e = p.addBody(0, 5.0f, 1, 5.0f, 0.0f);
    p.findBody(e)->radiusFwd = 0.8f + 2.0f * 1.6f;
    int f[4]; float fStart[4] = {70,72,74,76};
    for (int i = 0; i < 4; i++)
        f[i] = p.addBody(0, fStart[i], 1, 1.0f, 0.5f);

    for (int frame = 0; frame < 900; frame++) {
        p.findBody(e)->speed = 10.0f;
        p.step(t, 0.016f);
    }

    float pos[5];
    pos[0] = p.findBody(e)->distance;
    for (int i = 0; i < 4; i++) pos[1+i] = p.findBody(f[i])->distance;

    // Sorted order, no passthrough
    for (int i = 0; i < 4; i++)
        ASSERT(pos[i] < pos[i+1]);

    // Last car pinned at the buffer, chain packed behind it
    NEAR(pos[4], 100.0f - 0.8f, 0.15f);
    NEAR(pos[3], pos[4] - 1.6f, 0.15f);

    // Everything stopped dead: no bouncing, no reverse speeds
    for (int i = 0; i < 4; i++) {
        ASSERT(std::abs(p.findBody(f[i])->speed) < 0.1f);
    }
}

TEST(blocked_chain_stops_pusher) {
    auto t = makeLine(); game::PhysicsEngine p;
    int e = p.addBody(0, 90.0f, 1, 5.0f, 0.0f);
    int c = p.addBody(0, 95.0f, 1, 1.0f, 0.0f);
    for (int frame = 0; frame < 300; frame++) {
        p.findBody(e)->speed = 10.0f;
        p.step(t, 0.016f);
    }
    // Car against buffer, engine against car
    NEAR(p.findBody(c)->distance, 100.0f - 0.8f, 0.15f);
    NEAR(p.findBody(e)->distance, p.findBody(c)->distance - 1.6f, 0.15f);
    // Engine cannot keep moving: within a frame its speed collapses
    p.findBody(e)->speed = 10.0f;
    p.step(t, 0.016f);
    ASSERT(p.findBody(e)->moved < 0.01f);
}

// ======================== DECOUPLING ========================

TEST(decouple_and_coast) {
    auto t = makeLine(); game::PhysicsEngine p;
    // Engine moving at 6 decouples 2 cars behind it (they face backward,
    // so their projected speed is -6 in their own frame)
    int e = p.addBody(0, 40.0f, 1, 3.0f, 0.0f);
    p.findBody(e)->speed = 8.0f;

    int c1 = p.addBody(0, 38.4f, -1, 1.0f, 2.0f);
    int c2 = p.addBody(0, 36.8f, -1, 1.0f, 2.0f);
    p.findBody(c1)->speed = -6.0f;
    p.findBody(c2)->speed = -6.0f;

    for (int i = 0; i < 120; i++) p.step(t, 0.016f);

    auto *be=p.findBody(e), *b1=p.findBody(c1), *b2=p.findBody(c2);
    // Engine pulled away; cars coasted forward then slowed
    ASSERT(be->distance > b1->distance);
    ASSERT(b1->distance > 38.4f);
    ASSERT(std::abs(b1->speed) < 6.0f);
    // Cars stay in order and never interpenetrate
    ASSERT(b1->distance > b2->distance);
    ASSERT(b1->distance - b2->distance >= 1.6f - 0.11f);
}

// ======================== TOPOLOGY ========================

TEST(collision_across_segments) {
    game::TrackGraph t;
    t.addNode(0,0); t.addNode(50,0); t.addNode(100,0);
    t.addSegment(0,1); t.addSegment(1,2);
    game::PhysicsEngine p;
    int a = p.addBody(0, 45.0f, 1, 1.0f, 0.0f);   // on segment 0
    int b = p.addBody(1, 3.0f, 1, 1.0f, 0.0f);    // on segment 1
    for (int i = 0; i < 200; i++) {
        p.findBody(a)->speed = 5.0f;
        p.step(t, 0.016f);
    }
    // a pushed b along; b never got run through
    ASSERT(p.findBody(b)->distance > 3.0f);
    ASSERT(p.findBody(a)->segment <= p.findBody(b)->segment);
}

TEST(collision_on_flipped_segment) {
    // Second segment oriented the other way: nodeB of both meet in the middle
    game::TrackGraph t;
    t.addNode(0,0); t.addNode(100,0); t.addNode(50,0);
    t.addSegmentLen(0,2,50.0f);   // seg 0: 0 -> middle
    t.addSegmentLen(1,2,50.0f);   // seg 1: end -> middle (flipped)
    game::PhysicsEngine p;
    int a = p.addBody(0, 45.0f, 1, 1.0f, 0.0f);   // heading toward the middle
    int b = p.addBody(1, 45.0f, 1, 1.0f, 0.0f);   // 5 from middle on the far side
    for (int i = 0; i < 200; i++) {
        p.findBody(a)->speed = 5.0f;
        p.step(t, 0.016f);
    }
    // a crossed the node and pushed b backward along segment 1
    ASSERT(p.findBody(b)->distance < 45.0f);
}

TEST(switch_diverging_no_collision) {
    // Stem splits at a switch; car parked on the reverse leg must not
    // block a train routed onto the normal leg (1D track-space only)
    game::TrackGraph t;
    int n0 = t.addNode(0,0), n1 = t.addNode(50,0);
    int n2 = t.addNode(100,0), n3 = t.addNode(100,20);
    int stem = t.addSegment(n0,n1);
    int norm = t.addSegment(n1,n2);
    int rev  = t.addSegmentLen(n1,n3,54.0f);
    t.addSwitch(n1, stem, norm, rev);
    game::PhysicsEngine p;
    int a = p.addBody(stem, 45.0f, 1, 1.0f, 0.0f);
    int c = p.addBody(rev, 5.0f, 1, 1.0f, 0.0f);   // parked just past the frog
    for (int i = 0; i < 100; i++) {
        p.findBody(a)->speed = 5.0f;
        p.step(t, 0.016f);
    }
    ASSERT(p.findBody(a)->segment == norm);          // sailed through
    NEAR(p.findBody(c)->distance, 5.0f, 0.01f);      // untouched
}

TEST(persistent_bodies) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 20.0f, 1, 1.0f, 0.0f);
    p.removeBody(a);
    p.step(t, 0.016f);
    ASSERT(p.findBody(a) == nullptr);
    ASSERT(p.findBody(b) != nullptr);
    ASSERT(p.getBodies().size() == 1);
    int c = p.addBody(0, 30.0f, 1, 1.0f, 0.0f);
    ASSERT(c != b && p.findBody(c) != nullptr);
}

// ======================== GAME SIMULATION ========================

TEST(game_sim_ram_buffer) {
    auto t = makeLine(); game::PhysicsEngine p;
    // Engine with 2 coupled cars (radius), 4 free cars, ram everything home
    int e = p.addBody(0, 5.0f, 1, 5.0f, 0.0f);
    p.findBody(e)->radiusFwd = 0.8f + 2.0f * 1.6f;
    int f[4]; float fStart[4] = {70,72,74,76};
    for (int i = 0; i < 4; i++)
        f[i] = p.addBody(0, fStart[i], 1, 1.0f, 0.5f);

    for (int frame = 0; frame < 900; frame++) {
        p.findBody(e)->speed = 10.0f;
        p.step(t, 0.016f);
    }

    float pos[5];
    pos[0] = p.findBody(e)->distance + 4.0f;   // engine leading edge
    for (int i = 0; i < 4; i++) pos[1+i] = p.findBody(f[i])->distance;

    for (int i = 0; i < 4; i++)
        ASSERT(pos[i] < pos[i+1]);

    ASSERT(pos[4] <= 100.0f);

    // No reverse speeds anywhere
    ASSERT(p.findBody(e)->speed >= -0.01f);
    for (int i = 0; i < 4; i++)
        ASSERT(p.findBody(f[i])->speed >= -0.01f);
}

// ======================== SWITCH / JUNCTION FOULING ========================

// Build a Y switch: stem(n0-n1) splits at n1 into normal(n1-n2) and reverse(n1-n3).
static game::TrackGraph makeSwitch(int& stem, int& norm, int& rev) {
    game::TrackGraph t;
    int n0=t.addNode(0,0), n1=t.addNode(50,0), n2=t.addNode(100,0), n3=t.addNode(100,20);
    stem=t.addSegment(n0,n1); norm=t.addSegment(n1,n2); rev=t.addSegmentLen(n1,n3,54.0f);
    t.addSwitch(n1, stem, norm, rev);
    return t;
}

TEST(switch_frog_foul_blocks_crossing) {
    // A car parked fouling the frog on the reverse leg must block a train coming
    // through the switch stem->normal; they must not pass through each other.
    int stem,norm,rev; auto t=makeSwitch(stem,norm,rev);
    game::PhysicsEngine p;
    int a=p.addBody(stem,45.0f,1,1.0f,0.0f);   // approaching frog on the stem
    int c=p.addBody(rev,0.5f,1,1.0f,0.0f);     // fouling the frog on the reverse leg
    for (int i=0;i<200;i++){ p.findBody(a)->speed=4.0f; p.step(t,0.016f); }
    ASSERT(p.findBody(a)->segment == stem);          // stopped at the frog, never onto normal
    NEAR(p.findBody(c)->distance, 0.5f, 0.1f);        // fouling car essentially untouched
}

TEST(switch_converging_no_passthrough) {
    // Two cars converge on the frog from the stem and the reverse leg. Neither
    // may slide through the junction past the other.
    int stem,norm,rev; auto t=makeSwitch(stem,norm,rev);
    game::PhysicsEngine p;
    int a=p.addBody(stem,45.0f,1,1.0f,0.0f);
    int b=p.addBody(rev,3.0f,-1,1.0f,0.0f);   // facing the frog
    for (int i=0;i<200;i++){ p.findBody(a)->speed=4.0f; p.findBody(b)->speed=4.0f; p.step(t,0.016f); }
    // Never on the far leg past the other: A must not reach onto normal.
    ASSERT(!(p.findBody(a)->segment==norm && p.findBody(a)->distance>1.0f));
}

TEST(switch_clear_car_still_passes) {
    // A car parked well clear of the frog on the reverse leg must NOT block the
    // normal route (no false stalls at switches).
    int stem,norm,rev; auto t=makeSwitch(stem,norm,rev);
    game::PhysicsEngine p;
    int a=p.addBody(stem,45.0f,1,1.0f,0.0f);
    int c=p.addBody(rev,10.0f,1,1.0f,0.0f);    // 10 units down the reverse leg — clear
    for (int i=0;i<200;i++){ p.findBody(a)->speed=4.0f; p.step(t,0.016f); }
    ASSERT(p.findBody(a)->segment == norm);     // sailed through
    NEAR(p.findBody(c)->distance, 10.0f, 0.01f);
}

// ======================== MAIN ========================
int main() {
    printf("Track Physics Tests:\n");
    RUN(single_body_moves);
    RUN(reverse_moves_backward);
    RUN(friction_stops);
    RUN(coasting_decelerates);
    RUN(buffer_stops);
    RUN(radius_stops_short_of_buffer);
    RUN(no_tunneling_at_high_speed);
    RUN(collision_no_passthrough);
    RUN(momentum_transfer);
    RUN(heavy_pushes_light);
    RUN(head_on_collision);
    RUN(no_transfer_when_separating);
    RUN(chain_rams_buffer_all_stop);
    RUN(blocked_chain_stops_pusher);
    RUN(decouple_and_coast);
    RUN(collision_across_segments);
    RUN(collision_on_flipped_segment);
    RUN(switch_diverging_no_collision);
    RUN(switch_frog_foul_blocks_crossing);
    RUN(switch_converging_no_passthrough);
    RUN(switch_clear_car_still_passes);
    RUN(persistent_bodies);
    RUN(game_sim_ram_buffer);
    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail > 0 ? 1 : 0;
}
