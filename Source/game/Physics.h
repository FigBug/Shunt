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

// Hybrid track physics: bodies keep a single degree of freedom (arc length
// along the track), but collision detection is 2D — each vehicle is an oriented
// box placed at its world position, and contact impulses are projected back
// onto each body's track tangent. Pushing, wedging at switch frogs and buffer
// packing all fall out of one sequential-impulse solve instead of being
// special-cased in track space.
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
    // One oriented box on the track, owned by a body. A single vehicle is one
    // box; a consist (engine with its coupled cars, encoded by radiusFwd/Back)
    // is a short chain of boxes that follows the rail through curves.
    struct Collider
    {
        int   body = 0;      // index into `bodies`
        float cx = 0.0f, cy = 0.0f;   // centre, world
        float ux = 1.0f, uy = 0.0f;   // unit heading (+distance direction)
        float faceX = 1.0f, faceY = 0.0f;  // facing dir (body speed>0 moves this way)
        float halfLen = 0.8f, halfWid = 0.35f;
    };

    // A resolved contact between two colliders (or a body and a buffer, when
    // `bodyB` < 0), expressed as a normal in world space plus the tangent
    // projection factors that map a normal impulse onto each body's 1 DOF.
    struct ContactC
    {
        int bodyA = 0, bodyB = -1;
        float nx = 0.0f, ny = 0.0f;   // unit normal, world (points A -> B)
        float jA = 0.0f, jB = 0.0f;   // face . n for each body (velocity Jacobian)
        float pen = 0.0f;             // penetration depth
        float px = 0.0f, py = 0.0f;   // contact point (for sound placement)
    };

    void buildColliders (const TrackGraph& track);
    void detectContacts (const TrackGraph& track, float dt);

    std::vector<PhysBody> bodies;
    std::vector<Collider> colliders;
    std::vector<ContactC> contactC;
    std::vector<Contact>  contacts;
    int nextBodyId = 0;

    static constexpr int   kSubSteps      = 4;
    static constexpr int   kSolverIters   = 40;
    static constexpr float kMaxSpeed      = 30.0f;
    static constexpr float kHalfLen       = 0.8f;    // vehicle half length
    static constexpr float kHalfWid       = 0.34f;   // vehicle half width
    static constexpr float kRestitution   = 0.0f;    // trains do not bounce
    static constexpr float kSlop          = 0.02f;   // allowed penetration
    static constexpr float kBaumgarte     = 0.35f;   // positional correction fraction
    static constexpr float kContactEps    = 0.001f;
};

} // namespace game
