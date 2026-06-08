#include "GameState.h"
#include "BinaryData.h"
#include <gin_controllers/gin_controllers.h>
#include <algorithm>
#include <cmath>

namespace game
{

namespace
{
    constexpr int kMaxPlayers = 4;

    const juce::Colour kSlotColours[kMaxPlayers] {
        juce::Colour::fromRGB (255, 140,   0),   // orange
        juce::Colour::fromRGB (180,  50, 200),   // purple
        juce::Colour::fromRGB (  0, 200, 200),   // cyan
        juce::Colour::fromRGB (200, 200, 200),   // white/silver
    };

    juce::Random& rng()
    {
        static juce::Random r;
        return r;
    }
}

GameState::GameState (int numPlayers)
{
    auto jsonStr = juce::String::fromUTF8 (BinaryData::railgraph_json,
                                            BinaryData::railgraph_jsonSize);
    if (jsonStr.isNotEmpty())
        track.loadFromJson (jsonStr, 0.04f);
    else
        buildDefaultYard (track);

    // Shuffle drop-off assignments so each engine starts at a different one
    int numDZ = (int) track.getDropOffs().size();
    for (int i = 0; i < numDZ; ++i)
        spawnDropOffOrder.push_back (i);
    for (int i = numDZ - 1; i > 0; --i)
        std::swap (spawnDropOffOrder[(size_t) i],
                   spawnDropOffOrder[(size_t) rng().nextInt (i + 1)]);

    for (int i = 0; i < numPlayers; ++i)
        spawnAi (i);

    placeInitialCars();
}

GameState::SpawnInfo GameState::spawnPosNearDropOff (int dropOffIndex) const
{
    const auto& dropOffs = track.getDropOffs();
    if (dropOffIndex >= (int) dropOffs.size())
        return { { 0, 5.0f }, 1 };

    int dzNode = dropOffs[(size_t) dropOffIndex].node;

    for (int i = 0; i < track.numSegments(); ++i)
    {
        const auto& seg = track.getSegment (i);
        if (seg.nodeA == dzNode)
            return { { i, juce::jmin (3.0f, seg.length * 0.3f) }, 1 };   // dir=1 = away from nodeA (drop-off)
        if (seg.nodeB == dzNode)
            return { { i, seg.length - juce::jmin (3.0f, seg.length * 0.3f) }, -1 }; // dir=-1 = away from nodeB (drop-off)
    }

    return { { 0, 5.0f }, 1 };
}

void GameState::spawnPlayer (int controllerIndex, int slot)
{
    int dzIdx = (slot < (int) spawnDropOffOrder.size())
                    ? spawnDropOffOrder[(size_t) slot] : slot;
    auto spawn = spawnPosNearDropOff (dzIdx);

    Player p;
    p.controllerIndex = controllerIndex;
    p.slot   = slot;
    p.colour = kSlotColours[slot];
    p.pos    = spawn.pos;
    p.dir    = spawn.dir;
    p.facing = spawn.dir;

    for (auto& existing : players)
    {
        if (existing.slot == slot)
        {
            existing.controllerIndex = controllerIndex;
            existing.ai.reset();
            return;
        }
    }

    players.push_back (std::move (p));
}

void GameState::spawnAi (int slot)
{
    int dzIdx = (slot < (int) spawnDropOffOrder.size())
                    ? spawnDropOffOrder[(size_t) slot] : slot;
    auto spawn = spawnPosNearDropOff (dzIdx);

    Player p;
    p.controllerIndex = -1;
    p.slot   = slot;
    p.colour = kSlotColours[slot];
    p.pos    = spawn.pos;
    p.dir    = spawn.dir;
    p.facing = spawn.dir;

    p.ai = AiBrain {};

    players.push_back (std::move (p));
}

void GameState::placeInitialCars()
{
    // Find dead-end spur segments: reverse segments of switches that end at
    // a node with only one connected segment (a buffer stop).
    struct SpurInfo { int segment; int bufferNode; float length; float bufferDist; };
    std::vector<SpurInfo> spurs;

    for (const auto& sw : track.getSwitches())
    {
        int seg = sw.reverseSegment;
        const auto& s = track.getSegment (seg);

        // Check both endpoints — a dead-end spur has a node with only 1 segment
        for (int endpoint : { s.nodeA, s.nodeB })
        {
            int connections = 0;
            for (int i = 0; i < track.numSegments(); ++i)
            {
                const auto& other = track.getSegment (i);
                if (other.nodeA == endpoint || other.nodeB == endpoint)
                    ++connections;
            }

            if (connections == 1)
            {
                float bufferDist = (endpoint == s.nodeB) ? s.length : 0.0f;
                spurs.push_back ({ seg, endpoint, s.length, bufferDist });
                break;
            }
        }
    }

    // Remove spurs too short to hold even one car
    constexpr float kMinSpurLength = 3.0f;
    spurs.erase (std::remove_if (spurs.begin(), spurs.end(),
        [] (const SpurInfo& s) { return s.length < kMinSpurLength; }), spurs.end());

    if (spurs.empty()) return;

    // Build a shuffled list of colours: 5 of each colour = 20 cars
    std::vector<CarColour> colours;
    for (int i = 0; i < kInitialCars; ++i)
        colours.push_back ((CarColour) (i % (int) CarColour::count));

    for (int i = (int) colours.size() - 1; i > 0; --i)
    {
        int j = rng().nextInt (i + 1);
        std::swap (colours[(size_t) i], colours[(size_t) j]);
    }

    constexpr float kPlaceSpacing = 1.8f;

    // Calculate max cars per spur based on length
    int numSpurs = (int) spurs.size();
    std::vector<int> maxPerSpur ((size_t) numSpurs);
    int totalCapacity = 0;
    for (int i = 0; i < numSpurs; ++i)
    {
        maxPerSpur[(size_t) i] = juce::jmax (0, (int) ((spurs[(size_t) i].length - kPlaceSpacing) / kPlaceSpacing));
        totalCapacity += maxPerSpur[(size_t) i];
    }

    // Randomly assign each car to a spur
    std::vector<int> carsPerSpur ((size_t) numSpurs, 0);
    for (int i = 0; i < kInitialCars && i < totalCapacity; ++i)
    {
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            int si = rng().nextInt (numSpurs);
            if (carsPerSpur[(size_t) si] < maxPerSpur[(size_t) si])
            {
                carsPerSpur[(size_t) si]++;
                break;
            }
        }
    }

    int colourIdx = 0;
    for (int si = 0; si < numSpurs; ++si)
    {
        const auto& spur = spurs[(size_t) si];
        bool bufferAtEnd = (spur.bufferDist > spur.length * 0.5f);

        for (int ci = 0; ci < carsPerSpur[(size_t) si]; ++ci)
        {
            float dist;
            if (bufferAtEnd)
                dist = spur.length - (float) (ci + 1) * kPlaceSpacing;
            else
                dist = (float) (ci + 1) * kPlaceSpacing;

            if (dist < kPlaceSpacing || dist > spur.length - kPlaceSpacing)
                continue;

            if (colourIdx >= (int) colours.size()) break;

            Car car;
            car.id     = nextCarId++;
            car.colour = colours[(size_t) colourIdx++];
            car.pos    = { spur.segment, dist };
            car.dir    = 1;
            car.free   = true;
            cars.push_back (car);
        }
    }
}

void GameState::update (float dt, gin::GameControllerManager& controllers)
{
    if (gameOver || dt <= 0.0f)
        return;

    dt = juce::jmin (dt, 0.1f);

    for (auto& sw : track.getSwitches())
        sw.cooldown = juce::jmax (0.0f, sw.cooldown - dt);

    using B = gin::GameController::Button;
    using A = gin::GameController::Axis;

    for (auto& p : players)
    {
        if (p.ai.has_value())
        {
            aiUpdate (p, dt);
            continue;
        }

        if (p.controllerIndex < 0)
            continue;

        auto* c = controllers.getController (p.controllerIndex);
        if (c == nullptr || ! c->isConnected())
            continue;

        float throttle = -c->getAxis (A::leftY);

        if (c->isButtonDown (B::dpadUp))   throttle =  1.0f;
        if (c->isButtonDown (B::dpadDown)) throttle = -1.0f;

        float moveDist = throttle * kTrainSpeed * dt;

        bool toggleFwd  = c->isButtonDown (B::faceDown);    // A = switch ahead
        bool toggleBack = c->isButtonDown (B::faceRight);   // B = switch behind
        bool uncouple   = c->isButtonDown (B::faceUp);      // Y = decouple

        bool toggleFwdEdge  = toggleFwd  && ! p.prevToggleFwd;
        bool toggleBackEdge = toggleBack && ! p.prevToggleBack;
        bool uncoupleEdge   = uncouple   && ! p.prevUncouple;
        p.prevToggleFwd  = toggleFwd;
        p.prevToggleBack = toggleBack;
        p.prevUncouple   = uncouple;

        updatePlayer (p, moveDist, toggleFwdEdge, toggleBackEdge, uncoupleEdge);
    }

}

void GameState::updatePlayer (Player& p, float moveDist, bool toggleFwd, bool toggleBack, bool uncouple)
{
    if (std::abs (moveDist) > 0.001f)
    {
        bool forward = moveDist > 0.0f;
        int moveDir = forward ? p.dir : -p.dir;
        float dist = std::abs (moveDist);

        int leadCount = forward ? (int) p.frontCars.size()
                                : (int) p.rearCars.size();
        if (leadCount > 0)
        {
            float leadOffset = (float) leadCount * kCarSpacing;
            auto leadPos = track.advance (p.pos, moveDir, leadOffset);
            auto leadTest = track.advance (leadPos.pos, leadPos.dir, dist);
            if (leadTest.stopped)
            {
                auto leadStart = track.worldPos (leadPos.pos);
                auto leadEnd   = track.worldPos (leadTest.pos);
                float actualDist = leadStart.getDistanceFrom (leadEnd);
                dist = juce::jmin (dist, actualDist);
            }
        }
        else
        {
            auto test = track.advance (p.pos, moveDir, dist);
            if (test.stopped)
            {
                auto startW = track.worldPos (p.pos);
                auto endW   = track.worldPos (test.pos);
                dist = startW.getDistanceFrom (endW);
            }
        }

        dist = collisionLimit (p, moveDir, dist);

        if (dist > 0.001f)
        {
            auto result = track.advance (p.pos, moveDir, dist);
            p.pos = result.pos;
            p.dir = forward ? result.dir : -result.dir;
            p.facing = p.dir;
            p.lastMoveDir = result.dir;
            if (p.recoupleLock)
            {
                p.recoupleLockDist += dist;
                if (p.recoupleLockDist > kCarSpacing * 2.0f)
                    p.recoupleLock = false;
            }
        }
    }

    auto tryToggle = [&] (int searchDir)
    {
        // Search from the end of the consist in that direction
        int numCars = (searchDir == p.dir) ? (int) p.frontCars.size()
                                           : (int) p.rearCars.size();
        float offset = (float) numCars * kCarSpacing;
        auto endPos = track.advance (p.pos, searchDir, offset);
        auto switchNode = track.nextSwitchAhead (endPos.pos, endPos.dir);

        if (switchNode.has_value())
        {
            if (auto* sw = track.findSwitch (*switchNode))
            {
                if (sw->cooldown <= 0.0f && ! isSwitchOccupied (*switchNode))
                {
                    sw->reversed = ! sw->reversed;
                    sw->cooldown = 0.5f;
                }
            }
        }
    };

    if (toggleFwd)  tryToggle (p.dir);
    if (toggleBack) tryToggle (-p.dir);

    if (uncouple && p.totalCars() > 0)
    {
        auto dropAll = [&] (std::vector<int>& list, int dropDir)
        {
            for (int i = 0; i < (int) list.size(); ++i)
            {
                float offset = (float) (i + 1) * kCarSpacing;
                auto dropPos = track.advance (p.pos, dropDir, offset);

                for (auto& c : cars)
                    if (c.id == list[(size_t) i])
                    {
                        c.free = true;
                        c.pos = dropPos.pos;
                        c.dir = dropPos.dir;
                        break;
                    }
            }
            list.clear();
        };

        dropAll (p.frontCars, p.dir);
        dropAll (p.rearCars, -p.dir);
        p.hasColourLock = false;
        p.recoupleLock = true;
        p.recoupleLockDist = 0.0f;
    }

    checkCoupling (p);
    checkScoring (p);
}

void GameState::checkCoupling (Player& p)
{
    if (p.totalCars() >= kMaxConsist)
        return;

    if (p.recoupleLock)
        return;

    auto nextFrontSlot = track.worldPos (
        track.advance (p.pos, p.dir, (float) (p.frontCars.size() + 1) * kCarSpacing).pos);
    auto nextRearSlot = track.worldPos (
        track.advance (p.pos, -p.dir, (float) (p.rearCars.size() + 1) * kCarSpacing).pos);

    for (auto& c : cars)
    {
        if (! c.free) continue;

        if (p.hasColourLock && c.colour != p.carryColour)
            continue;

        auto carWorld = track.worldPos (c.pos);
        float df = nextFrontSlot.getDistanceFrom (carWorld);
        float dr = nextRearSlot.getDistanceFrom (carWorld);

        if (df < kCoupleDistance || dr < kCoupleDistance)
        {
            if (! p.hasColourLock)
            {
                p.carryColour = c.colour;
                p.hasColourLock = true;
            }

            c.free = false;
            if (df <= dr)
                p.frontCars.push_back (c.id);
            else
                p.rearCars.push_back (c.id);

            nextFrontSlot = track.worldPos (
                track.advance (p.pos, p.dir, (float) (p.frontCars.size() + 1) * kCarSpacing).pos);
            nextRearSlot = track.worldPos (
                track.advance (p.pos, -p.dir, (float) (p.rearCars.size() + 1) * kCarSpacing).pos);

            if (p.totalCars() >= kMaxConsist)
                return;
        }
    }
}

void GameState::checkScoring (Player& p)
{
    if (p.totalCars() == 0) return;

    for (const auto& dz : track.getDropOffs())
    {
        if ((int) p.carryColour != dz.colourIndex)
            continue;

        auto dzPos = track.getNode (dz.node).position;

        auto tryScore = [&] (std::vector<int>& list, bool front) -> bool
        {
            for (int i = (int) list.size() - 1; i >= 0; --i)
            {
                float cd = carWorldPos (p, front, i).getDistanceFrom (dzPos);
                if (cd < kScoreDistance)
                {
                    int carId = list[(size_t) i];
                    cars.erase (
                        std::remove_if (cars.begin(), cars.end(),
                            [carId] (const Car& c) { return c.id == carId; }),
                        cars.end());
                    list.erase (list.begin() + i);
                    p.score++;
                    return true;
                }
            }
            return false;
        };

        if (tryScore (p.frontCars, true) || tryScore (p.rearCars, false))
        {
            if (p.totalCars() == 0)
                p.hasColourLock = false;

            bool anyCarsLeft = false;
            for (const auto& c : cars)
                if (c.free) { anyCarsLeft = true; break; }
            for (const auto& other : players)
                if (other.totalCars() > 0) { anyCarsLeft = true; break; }

            if (! anyCarsLeft)
                gameOver = true;

            return;
        }
    }
}

bool GameState::isSwitchOccupied (int switchNode) const
{
    const auto* sw = track.findSwitch (switchNode);
    if (sw == nullptr) return false;

    auto nodePos = track.getNode (switchNode).position;
    constexpr float kOccupyRadius = 1.2f;

    auto isNear = [&] (TrackPos tp) -> bool
    {
        const auto& seg = track.getSegment (tp.segment);
        if (seg.nodeA != switchNode && seg.nodeB != switchNode)
            return false;
        return track.worldPos (tp).getDistanceFrom (nodePos) < kOccupyRadius;
    };

    for (const auto& p : players)
    {
        if (isNear (p.pos))
            return true;

        for (int i = 0; i < (int) p.frontCars.size(); ++i)
            if (isNear (track.advance (p.pos, p.dir, (float) (i + 1) * kCarSpacing).pos))
                return true;

        for (int i = 0; i < (int) p.rearCars.size(); ++i)
            if (isNear (track.advance (p.pos, -p.dir, (float) (i + 1) * kCarSpacing).pos))
                return true;
    }

    for (const auto& c : cars)
        if (c.free && isNear (c.pos))
            return true;

    return false;
}

bool GameState::canToggleSwitch (int switchNode) const
{
    const auto* sw = track.findSwitch (switchNode);
    if (sw == nullptr) return false;
    return sw->cooldown <= 0.0f && ! isSwitchOccupied (switchNode);
}

float GameState::collisionLimit (const Player& p, int moveDir, float maxDist) const
{
    bool forward = (moveDir == p.dir);
    int leadCount = forward ? (int) p.frontCars.size() : (int) p.rearCars.size();
    float leadOffset = (float) leadCount * kCarSpacing;

    auto leadPos = track.advance (p.pos, moveDir, leadOffset + kCarSpacing * 0.5f);
    auto leadWorld = track.worldPos (leadPos.pos);

    constexpr float kMinGap = 0.8f;

    auto checkVehicle = [&] (juce::Point<float> vw)
    {
        float gap = leadWorld.getDistanceFrom (vw) - kMinGap;
        if (gap < maxDist)
            maxDist = juce::jmax (0.0f, gap);
    };

    // Check other players' engines and coupled cars
    for (const auto& other : players)
    {
        if (&other == &p) continue;

        checkVehicle (track.worldPos (other.pos));

        for (int i = 0; i < (int) other.frontCars.size(); ++i)
            checkVehicle (carWorldPos (other, true, i));

        for (int i = 0; i < (int) other.rearCars.size(); ++i)
            checkVehicle (carWorldPos (other, false, i));
    }

    // Check free cars on the track
    for (const auto& c : cars)
    {
        if (! c.free) continue;

        // Skip cars of the colour we can couple (we'll couple them, not collide)
        if (! p.hasColourLock || c.colour == p.carryColour)
            continue;

        checkVehicle (track.worldPos (c.pos));
    }

    return maxDist;
}

juce::Point<float> GameState::carWorldPos (const Player& p, bool front, int index) const
{
    float dist = (float) (index + 1) * kCarSpacing;
    int walkDir = front ? p.dir : -p.dir;
    auto result = track.advance (p.pos, walkDir, dist);
    return track.worldPos (result.pos);
}

float GameState::carAngle (const Player& p, bool front, int index) const
{
    float dist = (float) (index + 1) * kCarSpacing;
    int walkDir = front ? p.dir : -p.dir;
    auto result = track.advance (p.pos, walkDir, dist);
    return track.trackAngle (result.pos, front ? p.dir : -p.dir);
}

// ============================================================================
// AI
// ============================================================================

void GameState::aiUpdate (Player& p, float dt)
{
    if (! p.ai.has_value()) return;
    auto& brain = *p.ai;

    brain.thinkTimer -= dt;
    brain.switchCooldown -= dt;

    bool rethink = brain.thinkTimer <= 0.0f;
    if (rethink)
        brain.thinkTimer = 0.2f;

    // State transitions — always check, not just on rethink
    if (brain.state == AiBrain::idle)
    {
        if (p.totalCars() > 0)
            brain.state = AiBrain::returningHome;
        else
            brain.state = AiBrain::seekingCar;
    }

    if (rethink)
    {

        // Stuck detection: if we haven't moved much, pick a new target
        auto curWorld = track.worldPos (p.pos);
        if (brain.lastPos.getDistanceFrom (curWorld) < 0.5f)
            brain.stuckCount++;
        else
            brain.stuckCount = 0;
        brain.lastPos = curWorld;

        if (brain.stuckCount > 15)
        {
            brain.targetCarId = -1;
            brain.stuckCount = 0;
        }
    }

    // Find target
    juce::Point<float> targetWorld;
    bool hasTarget = false;

    if (brain.state == AiBrain::seekingCar)
    {
        if (rethink && brain.targetCarId < 0)
        {
            auto locoWorld = track.worldPos (p.pos);
            std::vector<std::pair<float, int>> candidates;

            for (const auto& c : cars)
            {
                if (! c.free) continue;
                if (p.hasColourLock && c.colour != p.carryColour)
                    continue;
                float d = locoWorld.getDistanceFrom (track.worldPos (c.pos));
                candidates.push_back ({ d, c.id });
            }

            if (! candidates.empty())
            {
                std::sort (candidates.begin(), candidates.end());
                int pick = juce::jmin ((int) candidates.size() - 1, rng().nextInt (3));
                brain.targetCarId = candidates[(size_t) pick].second;
            }
        }

        if (brain.targetCarId >= 0)
        {
            for (const auto& c : cars)
                if (c.id == brain.targetCarId && c.free)
                {
                    targetWorld = track.worldPos (c.pos);
                    brain.targetPos = c.pos;
                    hasTarget = true;
                    break;
                }

            if (! hasTarget)
                brain.targetCarId = -1;
        }

        if (! hasTarget)
            return;
    }
    else if (brain.state == AiBrain::returningHome)
    {
        for (const auto& dz : track.getDropOffs())
        {
            if (dz.colourIndex == (int) p.carryColour)
            {
                int dzNode = dz.node;
                targetWorld = track.getNode (dzNode).position;
                // Find a segment touching this node and create a TrackPos
                for (int si = 0; si < track.numSegments(); ++si)
                {
                    const auto& seg = track.getSegment (si);
                    if (seg.nodeA == dzNode)
                        { brain.targetPos = { si, 0.0f }; break; }
                    if (seg.nodeB == dzNode)
                        { brain.targetPos = { si, seg.length }; break; }
                }
                hasTarget = true;
                break;
            }
        }

        if (! hasTarget)
            return;
    }

    if (! hasTarget)
        return;

    auto locoWorld = track.worldPos (p.pos);

    // Find path to target
    if (rethink)
    {
        auto result = track.findPath (p.pos, p.dir, brain.targetPos);
        brain.path = std::move (result.segments);
        brain.pathDir = result.firstDir;
    }

    if (brain.path.empty())
        return;

    // Find which path segment we're currently on
    int pathIdx = 0;
    for (int i = 0; i < (int) brain.path.size(); ++i)
        if (brain.path[(size_t) i] == p.pos.segment)
            { pathIdx = i; break; }

    // Determine direction: where is the next segment in the path?
    int moveDir = brain.pathDir;
    if (pathIdx + 1 < (int) brain.path.size())
    {
        int nextSeg = brain.path[(size_t) (pathIdx + 1)];
        const auto& curS = track.getSegment (p.pos.segment);
        const auto& nxtS = track.getSegment (nextSeg);

        // Find which end of current segment connects to next segment
        bool nextAtB = (nxtS.nodeA == curS.nodeB || nxtS.nodeB == curS.nodeB);
        bool nextAtA = (nxtS.nodeA == curS.nodeA || nxtS.nodeB == curS.nodeA);

        if (nextAtB && ! nextAtA)
            moveDir = 1;   // go toward nodeB
        else if (nextAtA && ! nextAtB)
            moveDir = -1;  // go toward nodeA
        else
            moveDir = brain.pathDir;
    }
    else
    {
        // On the target segment — go toward the target position
        moveDir = (brain.targetPos.distance > p.pos.distance) ? 1 : -1;
    }

    float throttle = (moveDir == p.dir) ? 1.0f : -1.0f;
    float moveDist = throttle * kTrainSpeed * 0.7f * dt;

    // Set switches along the path so the route is clear
    if (rethink && brain.switchCooldown <= 0.0f && brain.path.size() >= 2)
    {
        for (size_t pi = (size_t) juce::jmax (0, pathIdx); pi + 1 < brain.path.size(); ++pi)
        {
            int segA = brain.path[pi];
            int segB = brain.path[pi + 1];
            const auto& sA = track.getSegment (segA);
            const auto& sB = track.getSegment (segB);

            int sharedNode = -1;
            if (sA.nodeA == sB.nodeA || sA.nodeA == sB.nodeB) sharedNode = sA.nodeA;
            if (sA.nodeB == sB.nodeA || sA.nodeB == sB.nodeB) sharedNode = sA.nodeB;

            if (sharedNode < 0) continue;

            auto* sw = track.findSwitch (sharedNode);
            if (sw == nullptr) continue;
            if (sw->cooldown > 0.0f || isSwitchOccupied (sharedNode)) continue;

            int currentRoute = -1;
            if (segA == sw->stemSegment)
                currentRoute = sw->reversed ? sw->reverseSegment : sw->normalSegment;
            else if (segA == sw->normalSegment)
                currentRoute = sw->stemSegment;
            else if (segA == sw->reverseSegment)
                currentRoute = sw->stemSegment;

            if (currentRoute == segB)
                continue;

            sw->reversed = ! sw->reversed;

            int newRoute = -1;
            if (segA == sw->stemSegment)
                newRoute = sw->reversed ? sw->reverseSegment : sw->normalSegment;
            else if (segA == sw->normalSegment)
                newRoute = sw->stemSegment;
            else if (segA == sw->reverseSegment)
                newRoute = sw->stemSegment;

            if (newRoute == segB)
            {
                sw->cooldown = 0.5f;
                brain.switchCooldown = 0.3f;
                break;
            }
            else
            {
                sw->reversed = ! sw->reversed; // revert
            }
        }
    }

    updatePlayer (p, moveDist, false, false, false);

    // State transitions after movement
    if (brain.state == AiBrain::seekingCar && p.totalCars() > 0)
    {
        brain.state = AiBrain::returningHome;
        brain.targetCarId = -1;
        brain.path.clear();
    }
    else if (brain.state == AiBrain::returningHome && p.totalCars() == 0)
    {
        brain.state = AiBrain::idle;
        brain.path.clear();
    }
}

} // namespace game
