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
    int dir = 1;
    float velocity = 0.0f;
    float mass = 1.0f;
    float friction = 0.0f;
    float force = 0.0f;       // accumulated force this frame
    bool active = true;
};

struct PhysJoint
{
    int id = 0;
    int bodyA = -1;
    int bodyB = -1;
    float restLength = 1.6f;
    float stiffness = 5000.0f;
    float damping = 200.0f;
    bool active = true;
};

class PhysicsEngine
{
public:
    void clear();

    int addBody (int segment, float distance, int dir, float mass, float friction);
    void removeBody (int id);
    PhysBody* findBody (int id);
    const PhysBody* findBody (int id) const;

    int addJoint (int bodyA, int bodyB, float restLength);
    void removeJoint (int id);
    void removeJointsForBody (int bodyId);

    void step (const TrackGraph& track, float dt);

    float signedTrackDist (const TrackGraph& track, const PhysBody& from, const PhysBody& to) const;
    const std::vector<PhysBody>& getBodies() const { return bodies; }

private:
    std::vector<PhysBody> bodies;
    std::vector<PhysJoint> joints;
    int nextBodyId = 0;
    int nextJointId = 0;

    static constexpr float kMinGap = 1.2f;
    static constexpr float kCollisionStiffness = 5000.0f;
    static constexpr float kCollisionDamping = 200.0f;
    static constexpr float kMaxForce = 50000.0f;
};

} // namespace game
