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

    spawnCars();
}

void GameState::spawnPlayer (int controllerIndex, int slot)
{
    const auto* siding = track.findSiding (slot);
    if (siding == nullptr) return;

    Player p;
    p.controllerIndex = controllerIndex;
    p.slot   = slot;
    p.colour = kSlotColours[slot];

    const auto& seg = track.getSegment (siding->segment);
    p.pos = { siding->segment, seg.length * 0.9f };
    p.dir = -1;
    p.facing = -1;

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
    const auto* siding = track.findSiding (slot);
    if (siding == nullptr) return;

    Player p;
    p.controllerIndex = -1;
    p.slot   = slot;
    p.colour = kSlotColours[slot];

    const auto& seg = track.getSegment (siding->segment);
    p.pos = { siding->segment, seg.length * 0.9f };
    p.dir = -1;
    p.facing = -1;

    p.ai = AiBrain {};

    players.push_back (std::move (p));
}

void GameState::spawnCars()
{
    int freeCount = 0;
    for (const auto& c : cars)
        if (c.free) ++freeCount;

    while (freeCount < kMaxFreeCars)
    {
        int seg = rng().nextInt (5);
        const auto& s = track.getSegment (seg);
        float dist = rng().nextFloat() * s.length * 0.8f + s.length * 0.1f;

        bool tooClose = false;
        auto candidatePos = track.worldPos ({ seg, dist });
        for (const auto& c : cars)
        {
            if (! c.free) continue;
            if (track.worldPos (c.pos).getDistanceFrom (candidatePos) < kCarSpacing * 1.5f)
            {
                tooClose = true;
                break;
            }
        }
        for (const auto& p : players)
        {
            if (track.worldPos (p.pos).getDistanceFrom (candidatePos) < kCarSpacing * 2.0f)
            {
                tooClose = true;
                break;
            }
        }

        if (! tooClose)
        {
            Car car;
            car.id   = nextCarId++;
            car.pos  = { seg, dist };
            car.dir  = 1;
            car.free = true;
            cars.push_back (car);
            ++freeCount;
        }
        else
        {
            break;
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

    spawnTimer -= dt;
    if (spawnTimer <= 0.0f)
    {
        spawnCars();
        spawnTimer = kSpawnInterval;
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

        auto carWorld = track.worldPos (c.pos);
        float df = nextFrontSlot.getDistanceFrom (carWorld);
        float dr = nextRearSlot.getDistanceFrom (carWorld);

        if (df < kCoupleDistance || dr < kCoupleDistance)
        {
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

    const auto* siding = track.findSiding (p.slot);
    if (siding == nullptr) return;

    auto bufferPos = track.getNode (siding->bufferNode).position;
    auto locoWorld = track.worldPos (p.pos);

    if (locoWorld.getDistanceFrom (bufferPos) < kScoreDistance)
    {
        p.score += p.totalCars();

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
            if (swInfo != nullptr)
            {
                const auto* homeSiding = track.findSiding (p.slot);
                bool isHomeSw = homeSiding && *sw == homeSiding->switchNode;
                bool onSiding = homeSiding && p.pos.segment == homeSiding->segment;
                bool want = false;

                if (brain.state == AiBrain::seekingCar)
                    want = isHomeSw && onSiding;
                else
                    want = isHomeSw;

                if (swInfo->reversed != want
                    && swInfo->cooldown <= 0.0f
                    && ! isSwitchOccupied (*sw))
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
