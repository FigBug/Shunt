#pragma once

#include <vector>

namespace game
{

struct TrackPos;
class TrackGraph;

struct PhysBody
{
    int id = 0;
    int segment = 0;
    float distance = 0.0f;
    int dir = 1;              // facing: +1 = toward the segment's nodeB
    float speed = 0.0f;       // along facing; negative = reversing
    float mass = 1.0f;
    float friction = 0.0f;    // deceleration toward zero, units/s^2
    float radiusFwd = 0.8f;   // collision boundary ahead of pos, along facing
    float radiusBack = 0.8f;  // collision boundary behind pos
    float moved = 0.0f;       // distance travelled during the last step()
    bool active = true;
    bool isEngine = false;    // a driven train (never couples to another engine)
};

class PhysicsEngine
{
public:
    void clear();

    int addBody (int segment, float distance, int dir, float mass, float friction,
                 bool isEngine = false);
    void removeBody (int id);
    PhysBody* findBody (int id);
    const PhysBody* findBody (int id) const;

    void step (const TrackGraph& track, float dt);

    const std::vector<PhysBody>& getBodies() const { return bodies; }

    // Approaching impacts resolved during the last step(). `speed` is the
    // closing speed at contact — larger means a harder hit. Location is on the
    // track so the host can place a collision sound.
    struct Contact { int segment = 0; float distance = 0.0f; float speed = 0.0f; };
    const std::vector<Contact>& getContacts() const { return contacts; }

private:
    // A body's collision surface, transported along the track from its centre.
    // velDir is the coordinate direction of positive body speed at this point,
    // so motion can be compared between bodies on differently-oriented segments.
    struct Edge
    {
        int segment = 0;
        float distance = 0.0f;
        int velDir = 1;
    };

    struct BodyEdges { Edge front, back; };

    struct ScanHit
    {
        float gap = 1e9f;      // scan origin to nearest obstacle surface; negative = overlap
        int bodyIndex = -1;    // index into bodies, -1 = none
        bool buffer = false;
        bool foul = false;     // body fouling a shared node from another leg of a
                               // switch/crossing — block, but not colinear
        int walkDir = 0;       // coordinate walk direction at the hit
        int markerVelDir = 0;  // hit edge's velDir
    };

    void computeEdges (const TrackGraph& track, size_t index);
    ScanHit scanAhead (const TrackGraph& track, size_t selfIndex,
                       const Edge& from, int outDir, float maxRange) const;
    bool isBlockedAhead (const TrackGraph& track, size_t index,
                         int alongFacing, int depth) const;

    std::vector<PhysBody> bodies;
    std::vector<BodyEdges> edges;
    std::vector<Contact> contacts;
    int nextBodyId = 0;

    static constexpr int   kSubSteps = 4;
    static constexpr float kMaxSpeed = 30.0f;
    static constexpr float kOverlapWindow = 0.5f;
    static constexpr float kSeparationCap = 0.05f;
    static constexpr float kContactEps = 0.001f;
    static constexpr float kBlockGap = 0.01f;
};

} // namespace game
