// Joint-based physics engine tests
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

namespace juce {
    template<typename T> struct Point { T x = 0, y = 0; };
    template<typename T> T jmin(T a, T b) { return a < b ? a : b; }
    template<typename T> T jmax(T a, T b) { return a > b ? a : b; }
    template<typename T> T jlimit(T lo, T hi, T v) { return v < lo ? lo : v > hi ? hi : v; }
    template<typename T> struct MathConstants {};
    template<> struct MathConstants<float> {
        static constexpr float pi = 3.14159265f, twoPi = 6.28318530f, halfPi = 1.57079632f;
    };
}

namespace game {
struct TrackNode { juce::Point<float> position; };
struct TrackSegment { int nodeA=0,nodeB=0; float length=0.0f; std::vector<juce::Point<float>> polyline; };
struct SwitchInfo { int node=0,stemSegment=0,normalSegment=0,reverseSegment=0; bool reversed=false; float cooldown=0.0f; };
struct CrossingInfo { int node=0,pairA1=0,pairA2=0,pairB1=0,pairB2=0; };
struct TrackPos { int segment=0; float distance=0.0f; };

class TrackGraph {
public:
    std::vector<TrackNode> nodes;
    std::vector<TrackSegment> segments;
    std::vector<SwitchInfo> switches;
    std::vector<CrossingInfo> crossings;

    int addNode(juce::Point<float> p) { nodes.push_back({p}); return (int)nodes.size()-1; }
    int addSegment(int a, int b) {
        float len = std::hypot(nodes[b].position.x-nodes[a].position.x, nodes[b].position.y-nodes[a].position.y);
        segments.push_back({a,b,len,{}}); return (int)segments.size()-1;
    }
    const TrackNode& getNode(int i) const { return nodes[i]; }
    const TrackSegment& getSegment(int i) const { return segments[i]; }
    int numSegments() const { return (int)segments.size(); }
    const std::vector<SwitchInfo>& getSwitches() const { return switches; }
    const std::vector<CrossingInfo>& getCrossings() const { return crossings; }
    SwitchInfo* findSwitch(int) { return nullptr; }
    const SwitchInfo* findSwitch(int) const { return nullptr; }
    juce::Point<float> worldPos(TrackPos p) const {
        auto& s = segments[p.segment]; auto a = nodes[s.nodeA].position; auto b = nodes[s.nodeB].position;
        float t = s.length > 0 ? p.distance / s.length : 0;
        return { a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t };
    }
    float trackAngle(TrackPos, int d) const { return d > 0 ? 0.0f : 3.14159265f; }
    struct MoveResult { TrackPos pos; int dir=1; bool stopped=false; };
    MoveResult advance(TrackPos pos, int dir, float dist) const {
        if (dist <= 0) return {pos, dir, false};
        auto& seg = segments[pos.segment]; float nd = pos.distance + (float)dir * dist;
        if (nd >= 0 && nd <= seg.length) return {{pos.segment, nd}, dir, false};
        if (nd > seg.length) {
            float ov = nd - seg.length; int cn = seg.nodeB;
            for (int i = 0; i < (int)segments.size(); i++) { if (i == pos.segment) continue; auto& s = segments[i];
                if (s.nodeA == cn) return advance({i, 0.0f}, 1, ov);
                if (s.nodeB == cn) return advance({i, s.length}, -1, ov); }
            return {{pos.segment, seg.length}, dir, true};
        }
        float ov = -nd; int cn = seg.nodeA;
        for (int i = 0; i < (int)segments.size(); i++) { if (i == pos.segment) continue; auto& s = segments[i];
            if (s.nodeA == cn) return advance({i, 0.0f}, 1, ov);
            if (s.nodeB == cn) return advance({i, s.length}, -1, ov); }
        return {{pos.segment, 0.0f}, dir, true};
    }
};
} // namespace game

#define PHYSICS_TEST_MODE 1
#include "game/Physics.h"
#include "game/Physics.cpp"

static int gPass = 0, gFail = 0;
#define TEST(name) static void test_##name()
#define RUN(name) do { printf("  %-55s", #name); test_##name(); printf(" PASS\n"); gPass++; } while(0)
#define ASSERT(cond) do { if (!(cond)) { printf(" FAIL line %d: %s\n", __LINE__, #cond); gFail++; return; } } while(0)
#define NEAR(a,b,e) ASSERT(std::abs((a)-(b))<(e))

static game::TrackGraph makeLine() {
    game::TrackGraph t;
    t.addNode({0,0}); t.addNode({100,0});
    t.addSegment(0,1);
    return t;
}

// ======================== BASIC MOVEMENT ========================

TEST(single_body_moves) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    p.findBody(id)->velocity = 5.0f;
    p.step(t, 1.0f);
    NEAR(p.findBody(id)->distance, 15.0f, 0.5f);
}

TEST(friction_stops) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 50.0f, 1, 1.0f, 100.0f);
    p.findBody(id)->velocity = 1.0f;
    p.step(t, 1.0f);
    NEAR(p.findBody(id)->velocity, 0.0f, 0.1f);
}

TEST(buffer_stops) {
    auto t = makeLine(); game::PhysicsEngine p;
    int id = p.addBody(0, 95.0f, 1, 1.0f, 0.0f);
    p.findBody(id)->velocity = 20.0f;
    p.step(t, 1.0f);
    ASSERT(p.findBody(id)->distance <= 100.1f);
    NEAR(p.findBody(id)->velocity, 0.0f, 0.1f);
}

// ======================== JOINTS ========================

TEST(joint_keeps_distance) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 11.6f, 1, 1.0f, 0.0f);
    p.addJoint(a, b, 1.6f);
    p.findBody(a)->velocity = 5.0f;
    for (int i = 0; i < 60; i++) p.step(t, 0.016f);
    float gap = p.findBody(b)->distance - p.findBody(a)->distance;
    NEAR(gap, 1.6f, 0.3f);
    // Equal masses: speed averages to 2.5
    ASSERT(p.findBody(b)->velocity > 1.5f);
}

TEST(joint_chain_moves_together) {
    auto t = makeLine(); game::PhysicsEngine p;
    int e = p.addBody(0, 10.0f, 1, 3.0f, 0.0f);
    int c1 = p.addBody(0, 11.6f, 1, 1.0f, 0.0f);
    int c2 = p.addBody(0, 13.2f, 1, 1.0f, 0.0f);
    p.addJoint(e, c1, 1.6f);
    p.addJoint(c1, c2, 1.6f);
    p.findBody(e)->velocity = 8.0f;
    for (int i = 0; i < 60; i++) {
        p.findBody(e)->velocity = 8.0f;
        p.step(t, 0.016f);
    }
    auto* be = p.findBody(e); auto* b1 = p.findBody(c1); auto* b2 = p.findBody(c2);
    ASSERT(be->distance > 10.0f);
    NEAR(b1->distance - be->distance, 1.6f, 0.3f);
    NEAR(b2->distance - b1->distance, 1.6f, 0.3f);
}

TEST(joint_chain_stops_at_buffer) {
    auto t = makeLine(); game::PhysicsEngine p;
    int e = p.addBody(0, 80.0f, 1, 3.0f, 0.0f);
    int c1 = p.addBody(0, 81.6f, 1, 1.0f, 0.0f);
    int c2 = p.addBody(0, 83.2f, 1, 1.0f, 0.0f);
    p.addJoint(e, c1, 1.6f);
    p.addJoint(c1, c2, 1.6f);
    for (int i = 0; i < 300; i++) {
        p.findBody(e)->velocity = 10.0f;
        p.step(t, 0.016f);
    }
    auto* b2 = p.findBody(c2);
    ASSERT(b2->distance <= 100.1f);
    auto* be = p.findBody(e);
    ASSERT(be->distance < b2->distance);
    NEAR(p.findBody(c1)->distance - be->distance, 1.6f, 0.5f);
}

// ======================== COLLISIONS ========================

TEST(collision_no_passthrough) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 50.0f, 1, 1.0f, 0.0f);
    p.findBody(a)->velocity = 30.0f;
    for (int i = 0; i < 120; i++) p.step(t, 0.016f);
    ASSERT(p.findBody(a)->distance < p.findBody(b)->distance);
}

TEST(heavy_pushes_light) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 10.0f, 1, 5.0f, 0.0f);
    int b = p.addBody(0, 20.0f, 1, 1.0f, 0.0f); // closer so contact happens quickly
    p.findBody(a)->velocity = 8.0f;
    for (int i = 0; i < 300; i++) {
        p.findBody(a)->velocity = 8.0f;
        p.step(t, 0.016f);
    }
    ASSERT(p.findBody(b)->distance > 20.0f);
    ASSERT(p.findBody(a)->distance < p.findBody(b)->distance);
}

TEST(chain_pushes_free_cars) {
    auto t = makeLine(); game::PhysicsEngine p;
    int e = p.addBody(0, 5.0f, 1, 3.0f, 0.0f);
    int c1 = p.addBody(0, 6.6f, 1, 1.0f, 0.0f);
    p.addJoint(e, c1, 1.6f);
    int f1 = p.addBody(0, 30.0f, 1, 1.0f, 0.0f);
    int f2 = p.addBody(0, 32.0f, 1, 1.0f, 0.0f);
    for (int i = 0; i < 300; i++) {
        p.findBody(e)->velocity = 8.0f;
        p.step(t, 0.016f);
    }
    auto *be=p.findBody(e),*bc=p.findBody(c1),*bf1=p.findBody(f1),*bf2=p.findBody(f2);
    ASSERT(be->distance < bc->distance);
    ASSERT(bc->distance < bf1->distance);
    ASSERT(bf1->distance < bf2->distance);
    ASSERT(bf1->distance > 30.0f);
}

// ======================== GAME SIMULATION ========================

TEST(game_sim_ram_buffer) {
    auto t = makeLine(); game::PhysicsEngine p;
    // Engine + 2 coupled cars, 4 free cars, ram into buffer
    int e = p.addBody(0, 5.0f, 1, 3.0f, 0.0f);
    int c1 = p.addBody(0, 6.6f, 1, 1.0f, 0.0f);
    int c2 = p.addBody(0, 8.2f, 1, 1.0f, 0.0f);
    p.addJoint(e, c1, 1.6f);
    p.addJoint(c1, c2, 1.6f);
    int f[4]; float fStart[4] = {70,72,74,76};
    for (int i = 0; i < 4; i++)
        f[i] = p.addBody(0, fStart[i], 1, 1.0f, 0.0f);

    for (int frame = 0; frame < 600; frame++) {
        p.findBody(e)->velocity = 10.0f;
        p.step(t, 0.016f);
    }

    // All should be stacked at buffer
    float pos[7];
    pos[0] = p.findBody(e)->distance;
    pos[1] = p.findBody(c1)->distance;
    pos[2] = p.findBody(c2)->distance;
    for (int i = 0; i < 4; i++) pos[3+i] = p.findBody(f[i])->distance;

    // Sorted order, no passthrough
    for (int i = 0; i < 6; i++)
        ASSERT(pos[i] < pos[i+1]);

    // Last car at buffer
    ASSERT(pos[6] <= 100.1f);

    // No reverse speeds
    ASSERT(p.findBody(e)->velocity >= -0.5f);
    for (int i = 0; i < 4; i++)
        ASSERT(p.findBody(f[i])->velocity >= -0.5f);
}

TEST(decouple_and_coast) {
    auto t = makeLine(); game::PhysicsEngine p;
    // Engine at FRONT (highest distance), cars behind (lower distance)
    int c2 = p.addBody(0, 20.0f, 1, 1.0f, 2.0f);
    int c1 = p.addBody(0, 21.6f, 1, 1.0f, 2.0f);
    int e = p.addBody(0, 23.2f, 1, 3.0f, 0.0f);
    int j1 = p.addJoint(c2, c1, 1.6f);
    int j2 = p.addJoint(c1, e, 1.6f);

    // Move together
    for (int i = 0; i < 30; i++) {
        p.findBody(e)->velocity = 6.0f;
        p.step(t, 0.016f);
    }

    // Decouple
    p.removeJoint(j1);
    p.removeJoint(j2);

    // Engine accelerates away, cars coast and decelerate
    for (int i = 0; i < 120; i++) {
        p.findBody(e)->velocity = 8.0f;
        p.step(t, 0.016f);
    }

    auto *be=p.findBody(e), *b1=p.findBody(c1), *b2=p.findBody(c2);
    // Engine ahead (it was at the front and accelerated)
    ASSERT(be->distance > b1->distance);
    // Cars decelerated
    ASSERT(b1->velocity < 6.0f);
    // Cars in order
    ASSERT(b1->distance > b2->distance);
    // Cars didn't pass through each other
    ASSERT(b1->distance - b2->distance >= 1.0f);
}

TEST(head_on_collision) {
    auto t = makeLine(); game::PhysicsEngine p;
    int a = p.addBody(0, 20.0f, 1, 1.0f, 0.0f);
    int b = p.addBody(0, 80.0f, 1, 1.0f, 0.0f);
    p.findBody(a)->velocity = 8.0f;
    p.findBody(b)->velocity = -8.0f;
    for (int i = 0; i < 200; i++) p.step(t, 0.016f);
    ASSERT(p.findBody(a)->distance < p.findBody(b)->distance);
    ASSERT(p.findBody(b)->distance - p.findBody(a)->distance >= 1.0f);
}

// ======================== MAIN ========================
int main() {
    printf("Joint-Based Physics Tests:\n");
    RUN(single_body_moves);
    RUN(friction_stops);
    RUN(buffer_stops);
    RUN(joint_keeps_distance);
    RUN(joint_chain_moves_together);
    RUN(joint_chain_stops_at_buffer);
    RUN(collision_no_passthrough);
    RUN(heavy_pushes_light);
    RUN(chain_pushes_free_cars);
    RUN(game_sim_ram_buffer);
    RUN(decouple_and_coast);
    RUN(head_on_collision);
    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail > 0 ? 1 : 0;
}
