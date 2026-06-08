#include "GameState.h"
#include <gin_controllers/gin_controllers.h>
#include <algorithm>
#include <cmath>

namespace game
{

namespace
{
    constexpr int kMaxPlayers = 4;

    const juce::Colour kSlotColours[kMaxPlayers] {
        juce::Colour::fromRGB (230,  70,  70),
        juce::Colour::fromRGB ( 70, 140, 230),
        juce::Colour::fromRGB ( 80, 200,  90),
        juce::Colour::fromRGB (240, 200,  60),
    };

    juce::Random& rng()
    {
        static juce::Random r;
        return r;
    }
}

GameState::GameState (int numPlayers)
{
    buildDefaultYard (track);

    for (int i = 0; i < numPlayers; ++i)
        spawnAi (i);

    placeInitialCars();
}

TrackPos GameState::spawnPosNearDropOff (int dropOffIndex) const
{
    const auto& dropOffs = track.getDropOffs();
    if (dropOffIndex >= (int) dropOffs.size())
        return { 0, 5.0f };

    int dzNode = dropOffs[(size_t) dropOffIndex].node;

    for (int i = 0; i < track.numSegments(); ++i)
    {
        const auto& seg = track.getSegment (i);
        if (seg.nodeA == dzNode)
            return { i, 3.0f };
        if (seg.nodeB == dzNode)
            return { i, seg.length - 3.0f };
    }

    return { 0, 5.0f };
}

void GameState::spawnPlayer (int controllerIndex, int slot)
{
    auto startPos = spawnPosNearDropOff (slot);

    Player p;
    p.controllerIndex = controllerIndex;
    p.slot   = slot;
    p.colour = kSlotColours[slot];
    p.pos    = startPos;
    p.dir    = 1;
    p.facing = 1;

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
    auto startPos = spawnPosNearDropOff (slot);

    Player p;
    p.controllerIndex = -1;
    p.slot   = slot;
    p.colour = kSlotColours[slot];
    p.pos    = startPos;
    p.dir    = 1;
    p.facing = 1;

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

    // Distribute cars evenly across all spurs, placed from buffer end inward
    int numSpurs = (int) spurs.size();
    std::vector<int> carsPerSpur ((size_t) numSpurs, 0);

    for (int i = 0; i < kInitialCars; ++i)
        carsPerSpur[(size_t) (i % numSpurs)]++;

    constexpr float kPlaceSpacing = 1.8f;

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

        bool toggle   = c->isButtonDown (B::faceDown);
        bool uncouple = c->isButtonDown (B::faceRight);

        bool toggleEdge   = toggle   && ! p.prevToggle;
        bool uncoupleEdge = uncouple && ! p.prevUncouple;
        p.prevToggle   = toggle;
        p.prevUncouple = uncouple;

        updatePlayer (p, moveDist, toggleEdge, uncoupleEdge);
    }

    timeRemaining -= dt;
    if (timeRemaining <= 0.0f)
    {
        timeRemaining = 0.0f;
        gameOver = true;
    }
}

void GameState::updatePlayer (Player& p, float moveDist, bool toggle, bool uncouple)
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
        }
    }

    if (toggle)
    {
        auto switchNode = track.nextSwitchAhead (p.pos, p.lastMoveDir);
        if (switchNode.has_value())
        {
            if (auto* sw = track.findSwitch (*switchNode))
            {
                if (sw->cooldown <= 0.0f && ! isSwitchOccupied (*switchNode))
                {
                    sw->reversed = ! sw->reversed;
                    sw->cooldown = 2.0f;
                }
            }
        }
    }

    if (uncouple)
    {
        auto dropCar = [] (std::vector<int>& list, std::vector<Car>& allCars,
                           TrackPos pos, int dir)
        {
            if (list.empty()) return;
            int carId = list.back();
            list.pop_back();
            for (auto& c : allCars)
                if (c.id == carId)
                    { c.free = true; c.pos = pos; c.dir = dir; break; }
        };

        if (! p.rearCars.empty())
            dropCar (p.rearCars, cars, p.pos, p.dir);
        else
            dropCar (p.frontCars, cars, p.pos, p.dir);
    }

    checkCoupling (p);
    checkScoring (p);
}

void GameState::checkCoupling (Player& p)
{
    if (p.totalCars() >= kMaxConsist)
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

    auto locoWorld = track.worldPos (p.pos);

    for (const auto& dz : track.getDropOffs())
    {
        auto dzPos = track.getNode (dz.node).position;
        if (locoWorld.getDistanceFrom (dzPos) > kScoreDistance)
            continue;

        if ((int) p.carryColour != dz.colourIndex)
            continue;

        int scored = p.totalCars();
        p.score += scored;

        auto removeCars = [&] (std::vector<int>& list)
        {
            for (int carId : list)
                cars.erase (
                    std::remove_if (cars.begin(), cars.end(),
                        [carId] (const Car& c) { return c.id == carId; }),
                    cars.end());
            list.clear();
        };

        removeCars (p.frontCars);
        removeCars (p.rearCars);
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

    constexpr float kVehicleHalf = 0.5f;

    for (const auto& other : players)
    {
        if (&other == &p) continue;

        auto checkVehicle = [&] (juce::Point<float> vw)
        {
            float gap = leadWorld.getDistanceFrom (vw) - kVehicleHalf * kCarSpacing;
            if (gap < maxDist)
                maxDist = juce::jmax (0.0f, gap);
        };

        checkVehicle (track.worldPos (other.pos));

        for (int i = 0; i < (int) other.frontCars.size(); ++i)
            checkVehicle (carWorldPos (other, true, i));

        for (int i = 0; i < (int) other.rearCars.size(); ++i)
            checkVehicle (carWorldPos (other, false, i));
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
        brain.thinkTimer = 0.15f;

    if (rethink && brain.state == AiBrain::idle)
    {
        if (p.totalCars() > 0)
            brain.state = AiBrain::returningHome;
        else
            brain.state = AiBrain::seekingCar;
    }

    juce::Point<float> targetWorld;
    bool hasTarget = false;

    if (brain.state == AiBrain::seekingCar)
    {
        if (rethink)
        {
            float bestDist = 1e9f;
            int bestCar = -1;
            auto locoWorld = track.worldPos (p.pos);

            for (const auto& c : cars)
            {
                if (! c.free) continue;
                float d = locoWorld.getDistanceFrom (track.worldPos (c.pos));
                if (d < bestDist)
                {
                    bestDist = d;
                    bestCar  = c.id;
                }
            }

            brain.targetCarId = bestCar;
        }

        if (brain.targetCarId >= 0)
        {
            for (const auto& c : cars)
                if (c.id == brain.targetCarId)
                    { targetWorld = track.worldPos (c.pos); hasTarget = true; break; }
        }

        if (! hasTarget)
            return;
    }
    else if (brain.state == AiBrain::returningHome)
    {
        const auto* siding = track.findSiding (p.slot);
        if (siding == nullptr) return;

        targetWorld = track.getNode (siding->bufferNode).position;
        hasTarget = true;
    }

    if (! hasTarget)
        return;

    auto locoWorld = track.worldPos (p.pos);
    float fwdAngle = track.trackAngle (p.pos, p.dir);
    juce::Point<float> fwdDir (std::cos (fwdAngle), std::sin (fwdAngle));
    auto diff = targetWorld - locoWorld;
    float dot = fwdDir.x * diff.x + fwdDir.y * diff.y;

    float throttle = dot > 0.0f ? 1.0f : -1.0f;
    float moveDist = throttle * kTrainSpeed * 0.7f * dt;

    if (rethink && brain.switchCooldown <= 0.0f)
    {
        int lookDir = throttle > 0.0f ? p.dir : -p.dir;
        auto sw = track.nextSwitchAhead (p.pos, lookDir);
        if (sw.has_value())
        {
            auto* swInfo = track.findSwitch (*sw);
            if (swInfo != nullptr
                && swInfo->cooldown <= 0.0f
                && ! isSwitchOccupied (*sw))
            {
                const auto* homeSiding = track.findSiding (p.slot);
                bool isHomeSw = homeSiding && *sw == homeSiding->switchNode;
                bool onSiding = homeSiding && p.pos.segment == homeSiding->segment;
                bool onMainLine = track.isMainLine (p.pos.segment);

                bool want = false;
                if (brain.state == AiBrain::seekingCar)
                {
                    if (onSiding && isHomeSw)
                        want = true;
                    else if (onMainLine)
                        want = true;
                }
                else
                {
                    if (isHomeSw)
                        want = true;
                    else if (onMainLine)
                        want = true;
                }

                if (swInfo->reversed != want)
                {
                    swInfo->reversed = want;
                    swInfo->cooldown = 2.0f;
                    brain.switchCooldown = 0.5f;
                }
            }
        }
    }

    updatePlayer (p, moveDist, false, false);

    if (brain.state == AiBrain::seekingCar && p.totalCars() > 0)
    {
        brain.state = AiBrain::returningHome;
        brain.targetCarId = -1;
    }
    else if (brain.state == AiBrain::returningHome && p.totalCars() == 0)
    {
        brain.state = AiBrain::idle;
    }
}

} // namespace game
