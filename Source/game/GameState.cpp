#include "GameState.h"
#include "BinaryData.h"
#include "Maps.h"
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

    const char* aiStateName (AiBrain::State s)
    {
        switch (s)
        {
            case AiBrain::idle:          return "idle";
            case AiBrain::seekingCar:    return "seek";
            case AiBrain::returningHome: return "return";
            case AiBrain::leavingDropOff:return "leave";
            case AiBrain::wandering:     return "wander";
        }
        return "?";
    }

    const char* colourName (CarColour c)
    {
        switch (c)
        {
            case CarColour::red:    return "red";
            case CarColour::blue:   return "blue";
            case CarColour::green:  return "green";
            case CarColour::yellow: return "yellow";
            case CarColour::count:  break;
        }
        return "?";
    }
}

GameState::GameState (int numPlayers, int mapIndex)
{
    const auto& maps = getMaps();
    juce::String jsonStr;
    if (mapIndex >= 0 && mapIndex < (int) maps.size())
        jsonStr = maps[(size_t) mapIndex].file.loadFileAsString();

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

    // Seed the wind: a random heading at a random strength above the minimum.
    {
        float a = rng().nextFloat() * juce::MathConstants<float>::twoPi;
        float s = kWindMinStrength + rng().nextFloat() * (1.0f - kWindMinStrength);
        wind = { std::cos (a) * s, std::sin (a) * s };
        targetWind = wind;
        windChangeTimer = kWindChangeInterval;
    }

    openLog (numPlayers, mapIndex);
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

    for (auto& existing : players)
    {
        if (existing.slot == slot)
        {
            existing.controllerIndex = controllerIndex;
            existing.ai.reset();
            return;
        }
    }

    Player p;
    p.controllerIndex = controllerIndex;
    p.slot   = slot;
    p.colour = kSlotColours[slot];
    p.pos    = spawn.pos;
    p.dir    = spawn.dir;
    p.bodyId = physics.addBody (spawn.pos.segment, spawn.pos.distance,
                                spawn.dir, kEngineMass, 0.0f, true);

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
    p.bodyId = physics.addBody (spawn.pos.segment, spawn.pos.distance,
                                spawn.dir, kEngineMass, 0.0f, true);

    p.ai = AiBrain {};

    players.push_back (std::move (p));
}

float GameState::distanceToSwitch (TrackPos start, int dir, float limit) const
{
    int   seg     = start.segment;
    int   d       = dir;
    float posDist = start.distance;
    float dist    = 0.0f;

    for (int steps = 0; steps < 30; ++steps)
    {
        const auto& s = track.getSegment (seg);
        int nodeAhead = (d > 0) ? s.nodeB : s.nodeA;

        dist += (d > 0) ? (s.length - posDist) : posDist;
        if (dist > limit)
            return limit;
        if (track.findSwitch (nodeAhead) != nullptr)
            return dist;

        int next = track.routeThrough (seg, nodeAhead);   // walk through plain junctions
        if (next < 0)
            return limit;                                 // dead end this way

        const auto& ns = track.getSegment (next);
        d       = (ns.nodeA == nodeAhead) ? 1 : -1;
        posDist = (d > 0) ? 0.0f : ns.length;
        seg     = next;
    }
    return limit;
}

void GameState::placeCarsFromSpawns()
{
    const float carLen          = kCarSpacing;
    const float switchClearance = 2.0f * carLen;

    // Resolve spawns to track positions, dropping any that sit within two car
    // lengths of a switch (a car there would foul the points).
    std::vector<TrackPos> validSpawns;
    for (const auto& sp : track.getSpawns())
    {
        auto tp = track.nearestTrackPos (sp.pos);
        bool nearSwitch = distanceToSwitch (tp, 1, switchClearance) < switchClearance
                       || distanceToSwitch (tp, -1, switchClearance) < switchClearance;
        if (! nearSwitch)
            validSpawns.push_back (tp);
    }
    if (validSpawns.empty()) return;

    int total = juce::jmax (0, track.getCarCount());
    if (total == 0) return;

    // Balanced, shuffled colour pool so the level gets an even spread of the
    // four car colours regardless of how the total divides up.
    std::vector<CarColour> pool;
    for (int i = 0; i < total; ++i)
        pool.push_back ((CarColour) (i % (int) CarColour::count));
    for (int i = total - 1; i > 0; --i)
        std::swap (pool[(size_t) i], pool[(size_t) rng().nextInt (i + 1)]);

    std::vector<juce::Point<float>> occupied;
    auto isFree = [&] (juce::Point<float> w)
    {
        for (const auto& o : occupied)
            if (o.getDistanceFrom (w) < carLen * 0.9f)
                return false;
        return true;
    };

    for (int i = 0; i < total; ++i)
    {
        auto base = validSpawns[(size_t) rng().nextInt ((int) validSpawns.size())];

        // Search outward from the spawn in both directions for the closest free
        // slot, so cars fill the rail either side without overlapping.
        TrackPos slot;
        bool found = false;
        for (int step = 0; step < 40 && ! found; ++step)
        {
            int dirsToTry = (step == 0) ? 1 : 2;
            for (int k = 0; k < dirsToTry && ! found; ++k)
            {
                int dir = (k == 0) ? 1 : -1;
                auto cand = track.advance (base, dir, (float) step * carLen);
                if (isFree (track.worldPos (cand.pos)))
                {
                    slot = cand.pos;
                    found = true;
                }
            }
        }
        if (! found) continue;   // spawn's rail is full — skip this car

        occupied.push_back (track.worldPos (slot));

        Car car;
        car.id     = nextCarId++;
        car.colour = pool[(size_t) i];
        car.pos    = slot;
        car.dir    = 1;
        car.free   = true;
        car.bodyId = physics.addBody (slot.segment, slot.distance, 1, kCarMass, kCarFriction);
        cars.push_back (car);
    }
}

void GameState::placeInitialCars()
{
    // Prefer explicit spawns authored in the level editor when the level has any.
    if (! track.getSpawns().empty())
    {
        placeCarsFromSpawns();
        return;
    }

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
            car.bodyId = physics.addBody (spur.segment, dist, 1, kCarMass, kCarFriction);
            cars.push_back (car);
        }
    }
}

void GameState::update (float dt, gin::GameControllerManager& controllers)
{
    soundEvents.clear();   // cues raised this tick, drained by the host afterwards
                           // (cleared before any early-out so stale cues never replay)

    if (gameOver || dt <= 0.0f)
        return;

    dt = juce::jmin (dt, 0.1f);

    for (auto& sw : track.getSwitches())
        sw.cooldown = juce::jmax (0.0f, sw.cooldown - dt);

    // Work out each train's lined-up switch and ownership before input/AI, so a
    // Y press (and the AI) act on this frame's ring.
    updateSwitchTargets();

    using B = gin::GameController::Button;
    using A = gin::GameController::Axis;

    for (auto& p : players)
    {
        float speed = 0.0f;
        bool flipEdge = false, uncoupleEdge = false;
        p.hornHeld = false;   // cleared unless a held horn button says otherwise

        if (p.ai.has_value())
        {
            speed = aiUpdate (p, dt);
        }
        else if (p.controllerIndex >= 0)
        {
            auto* c = controllers.getController (p.controllerIndex);
            if (c != nullptr && c->isConnected())
            {
                float throttle = -c->getAxis (A::leftY);

                if (c->isButtonDown (B::dpadUp))   throttle =  1.0f;
                if (c->isButtonDown (B::dpadDown)) throttle = -1.0f;
                if (std::abs (throttle) < 0.05f)   throttle =  0.0f;

                speed = throttle * kTrainSpeed;

                bool flipBtn  = c->isButtonDown (B::faceUp);      // Y = throw lined-up switch
                bool uncouple = c->isButtonDown (B::faceRight);   // B = uncouple
                bool horn     = c->isButtonDown (B::faceLeft);    // X = horn

                flipEdge     = flipBtn  && ! p.prevToggleFwd;
                uncoupleEdge = uncouple && ! p.prevUncouple;
                p.prevToggleFwd = flipBtn;
                p.prevUncouple  = uncouple;

                p.hornHeld = horn;   // sustained: sound layer loops while held
            }
        }

        // Boost: the horn (players) or an AI burst spends the reserve for a
        // higher top speed. While boosting, the target speed is 1.5x; when it
        // stops, the target drops back and the ramp eases the train down — it
        // never snaps to the normal max.
        bool wantBoost;
        if (p.ai.has_value())
        {
            auto& brain = *p.ai;
            // Spend the reserve in bursts, but only when there's a worthwhile
            // reason (a clear run at a car or a drop-off): commit once it's fully
            // charged and keep spending until the reason passes or it runs dry.
            bool worth = aiWantsBoost (p);
            if (worth && p.boost >= 1.0f) brain.wantBoost = true;    // charged + a reason — commit
            if (! worth || p.boost <= 0.0f) brain.wantBoost = false; // no reason, or empty — recharge
            wantBoost = brain.wantBoost && std::abs (speed) > 0.1f;
        }
        else
        {
            wantBoost = p.hornHeld;
        }

        // Trigger the boost only with enough in reserve (>= kBoostTriggerLevel),
        // but once it's going let it run until the reserve is empty — p.boosting
        // carries last frame's state, so holding the horn drains straight to zero
        // instead of stuttering on and off near the trigger level.
        bool boosting = wantBoost
                        && (p.boosting ? p.boost > 0.0f
                                       : p.boost >= kBoostTriggerLevel);
        p.boost = boosting ? juce::jmax (0.0f, p.boost - dt / kBoostEmptyTime)
                           : juce::jmin (1.0f, p.boost + dt / kBoostFillTime);
        p.boosting = boosting;   // for darker smoke + the AI's horn
        if (boosting)
            speed *= kBoostSpeedMult;

        // The horn and the boost are one and the same: an engine sounds its horn
        // exactly while it's boosting. So the horn won't trigger below the reserve
        // threshold and cuts out the instant the reserve empties — and while it's
        // sounding the reserve is draining, never recharging.
        p.hornHeld = boosting;

        // The throttle sets a *target* speed; the engine ramps toward it. The
        // collision boundary extends over the coupled cars on each side.
        float actualSpeed = speed;
        if (auto* b = physics.findBody (p.bodyId))
        {
            b->mass       = kEngineMass + (float) p.totalCars() * kCarMass;
            b->radiusFwd  = kVehicleHalfLen + (float) p.frontCars.size() * kCarSpacing;
            b->radiusBack = kVehicleHalfLen + (float) p.rearCars.size()  * kCarSpacing;

            // Choose the force driving the change: powering under throttle,
            // coasting with no throttle, or braking when throttle opposes
            // current motion. Acceleration = force / mass, so a heavy consist
            // changes speed slowly.
            float cur  = b->speed;
            bool reversing  = (speed != 0.0f && cur != 0.0f && (speed > 0.0f) != (cur > 0.0f));
            // Easing down covers coasting to a stop AND drifting from the boosted
            // top speed back to normal — both shed speed gently, no snap.
            bool easingDown = ! reversing && std::abs (cur) > std::abs (speed) + 1.0e-4f;
            float force = reversing ? kBrakeForce : (easingDown ? kCoastForce : kEngineForce);

            float maxDv = force / b->mass * dt;
            b->speed = cur + juce::jlimit (-maxDv, maxDv, speed - cur);
            actualSpeed = b->speed;
        }

        // Decoupled cars inherit the engine's actual speed, so resolve actions
        // after the ramp.
        handleActions (p, actualSpeed, flipEdge, uncoupleEdge);
    }

    physics.step (track, dt);

    // Collision cue: attribute each impact to the nearest engine and let each
    // engine sound at most once every 2s, so a grind or a multi-substep crash
    // doesn't machine-gun the sound.
    for (auto& p : players)
    {
        p.collisionCooldown = juce::jmax (0.0f, p.collisionCooldown - dt);
        p.ramCooldown       = juce::jmax (0.0f, p.ramCooldown - dt);
    }

    constexpr float kCollisionSpeed = 2.5f;   // closing speed to count as a crash
    for (const auto& ct : physics.getContacts())
    {
        if (ct.speed <= kCollisionSpeed) continue;

        auto cw = track.worldPos ({ ct.segment, ct.distance });

        Player* nearest = nullptr;
        float best = 1.0e9f;
        for (auto& p : players)
        {
            float d = track.worldPos (p.pos).getDistanceFrom (cw);
            if (d < best) { best = d; nearest = &p; }
        }

        if (nearest != nullptr && nearest->collisionCooldown <= 0.0f)
        {
            soundEvents.push_back ({ SoundEvent::collision, cw });
            nearest->collisionCooldown = 2.0f;
        }
    }

    // Read positions back from the physics bodies
    for (auto& p : players)
    {
        if (auto* b = physics.findBody (p.bodyId))
        {
            p.pos = { b->segment, b->distance };
            p.dir = b->dir;
            p.speed = b->speed;

            // A train trailing through a switch (leg -> stem) forces the points
            // to the leg it came from. This also keeps the trailing cars — whose
            // positions are derived by advancing across the switch — on their
            // real track instead of snapping onto the other leg.
            if (p.prevSegment >= 0 && p.prevSegment != p.pos.segment)
            {
                for (auto& sw : track.getSwitches())
                    if (p.pos.segment == sw.stemSegment
                        && (p.prevSegment == sw.normalSegment || p.prevSegment == sw.reverseSegment))
                    {
                        bool aligned = (p.prevSegment == sw.reverseSegment);
                        if (sw.reversed != aligned)   // points actually moved (trailed through)
                        {
                            sw.reversed = aligned;
                            soundEvents.push_back ({ SoundEvent::points,
                                                     track.getNode (sw.node).position });
                        }
                    }
            }
            p.prevSegment = p.pos.segment;

            if (p.recoupleLock)
            {
                p.recoupleLockDist += b->moved;
                if (p.recoupleLockDist > kCarSpacing * 2.0f)
                    p.recoupleLock = false;
            }
        }
    }

    for (auto& c : cars)
    {
        if (! c.free) continue;

        if (auto* b = physics.findBody (c.bodyId))
        {
            c.pos = { b->segment, b->distance };
            c.dir = b->dir;
        }
    }

    // Ram-stealing: a hard train-to-train hit shears the outermost car off the
    // end that was struck, on each consist carrying a car there. The freed car
    // rolls loose at the impact point for anyone (the rammer included) to grab.
    for (const auto& ct : physics.getContacts())
    {
        if (ct.speed < kRamBreakSpeed) continue;

        Player* a = playerForBody (ct.bodyA);
        Player* b = playerForBody (ct.bodyB);
        if (a == nullptr || b == nullptr) continue;   // a buffer or a free car — not a steal

        juce::Point<float> hit { ct.px, ct.py };
        tryRamBreak (*a, hit);
        tryRamBreak (*b, hit);
    }

    for (auto& p : players)
    {
        // Expire the delivery streak when no car has been dropped off recently.
        if (p.deliveryTimer > 0.0f)
        {
            p.deliveryTimer -= dt;
            if (p.deliveryTimer <= 0.0f)
                p.deliveryStreak = 0;
        }

        checkCoupling (p);
        checkScoring (p);
    }

    updateWeather (dt);

    // Diagnostic log: note discrete events as they happen, and a periodic
    // snapshot of every train/car and what each AI is doing.
    logClock += dt;
    for (const auto& ev : soundEvents)
    {
        const char* name = nullptr;
        switch (ev.type)
        {
            case SoundEvent::couple:    name = "COUPLE";    break;
            case SoundEvent::uncouple:  name = "UNCOUPLE";  break;
            case SoundEvent::collision: name = "COLLISION"; break;
            case SoundEvent::score:     name = "SCORE";     break;
            case SoundEvent::points:    name = "POINTS";    break;
            case SoundEvent::horn:      break;   // too frequent/noisy to log
        }
        if (name != nullptr)
            logLine ("[t=" + juce::String (logClock, 2) + "] EVENT " + name
                     + " at (" + juce::String (ev.worldPos.x, 1) + ","
                     + juce::String (ev.worldPos.y, 1) + ")");
    }

    logTimer     += dt;
    logCarsTimer += dt;
    if (logTimer >= 0.2f)
    {
        logTimer = 0.0f;
        bool includeCars = (logCarsTimer >= 1.0f);
        if (includeCars) logCarsTimer = 0.0f;
        logSnapshot (includeCars);
    }

    if (gameOver)
        logLine ("[t=" + juce::String (logClock, 2) + "] GAME OVER");
}

void GameState::handleActions (Player& p, float engineSpeed, bool flip, bool uncouple)
{
    // Y throws the switch this train is lined up on — the next one ahead in its
    // direction of travel — but only when it owns it and it can be thrown.
    if (flip && p.ringSwitch >= 0 && p.ringFlippable)
        flipSwitch (p.ringSwitch);

    if (uncouple && p.totalCars() > 0)
        decoupleAll (p, engineSpeed);
}

void GameState::decoupleAll (Player& p, float engineSpeed)
{
    if (p.totalCars() == 0) return;

    auto dropAll = [&] (std::vector<int>& list, int dropDir, float carSpeed)
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
                    c.bodyId = physics.addBody (dropPos.pos.segment, dropPos.pos.distance,
                                                dropPos.dir, kCarMass, kCarFriction);
                    physics.findBody (c.bodyId)->speed = carSpeed;
                    break;
                }
        }
        list.clear();
    };

    // Each car keeps the engine's speed, projected onto its own direction:
    // rear cars face away from the engine, so the sign flips
    dropAll (p.frontCars, p.dir, engineSpeed);
    dropAll (p.rearCars, -p.dir, -engineSpeed);
    soundEvents.push_back ({ SoundEvent::uncouple, track.worldPos (p.pos) });
    p.hasColourLock = false;
    p.recoupleLock = true;
    p.recoupleLockDist = 0.0f;
}

Player* GameState::playerForBody (int bodyId)
{
    if (bodyId < 0) return nullptr;
    for (auto& p : players)
        if (p.bodyId == bodyId)
            return &p;
    return nullptr;   // a free car's body, not an engine
}

void GameState::tryRamBreak (Player& victim, juce::Point<float> hit)
{
    if (victim.ramCooldown > 0.0f) return;    // already shed a car this hit
    if (victim.totalCars() == 0)   return;

    // Which end took the hit? Compare the impact point to each end's outermost
    // vehicle. An end with no cars is the bare engine, so a hit there is a shove,
    // not a theft — matching "ram the tail to steal, ram the loco to just push".
    auto frontEdge = track.worldPos (track.advance (victim.pos,  victim.dir,
                        (float) victim.frontCars.size() * kCarSpacing).pos);
    auto rearEdge  = track.worldPos (track.advance (victim.pos, -victim.dir,
                        (float) victim.rearCars.size()  * kCarSpacing).pos);

    bool front = hit.getDistanceFrom (frontEdge) < hit.getDistanceFrom (rearEdge);
    const auto& list = front ? victim.frontCars : victim.rearCars;
    if (list.empty()) return;                 // struck a bare engine end — a shove

    // A hard hit on a car scatters the whole consist: every car uncouples and
    // rolls loose for anyone (the rammer included) to grab. decoupleAll clears
    // both ends and sets the recouple lock; rate-limit it so one collision only
    // shears the train once.
    victim.ramCooldown = kRamCooldown;
    decoupleAll (victim, victim.speed);
}

void GameState::checkCoupling (Player& p)
{
    if (p.totalCars() >= kMaxConsist)
        return;

    if (p.recoupleLock)
        return;

    // The coupling slot is found by advancing along the track through switches,
    // so it lands on the routed branch. Coupling additionally requires the car
    // to be on that same segment, so an engine can't grab a car sitting on the
    // *other* branch of a switch just because it's close in world space.
    auto frontSlot = track.advance (p.pos,  p.dir, (float) (p.frontCars.size() + 1) * kCarSpacing).pos;
    auto rearSlot  = track.advance (p.pos, -p.dir, (float) (p.rearCars.size()  + 1) * kCarSpacing).pos;

    for (auto& c : cars)
    {
        if (! c.free) continue;

        if (p.hasColourLock && c.colour != p.carryColour)
            continue;

        auto carWorld = track.worldPos (c.pos);
        float df = (c.pos.segment == frontSlot.segment)
                     ? track.worldPos (frontSlot).getDistanceFrom (carWorld) : 1.0e9f;
        float dr = (c.pos.segment == rearSlot.segment)
                     ? track.worldPos (rearSlot).getDistanceFrom (carWorld) : 1.0e9f;

        if (df < kCoupleDistance || dr < kCoupleDistance)
        {
            if (! p.hasColourLock)
            {
                p.carryColour = c.colour;
                p.hasColourLock = true;
            }

            // Coupled cars lose their physics body: they ride rigidly
            // with the engine, covered by its collision radius
            c.free = false;
            physics.removeBody (c.bodyId);
            c.bodyId = -1;
            if (df <= dr)
                p.frontCars.push_back (c.id);
            else
                p.rearCars.push_back (c.id);

            soundEvents.push_back ({ SoundEvent::couple, carWorld });

            frontSlot = track.advance (p.pos,  p.dir, (float) (p.frontCars.size() + 1) * kCarSpacing).pos;
            rearSlot  = track.advance (p.pos, -p.dir, (float) (p.rearCars.size()  + 1) * kCarSpacing).pos;

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

        // Deliver the whole consist in one go once the engine itself is centred
        // on the drop-off. The drop-off lives on a specific track, so require the
        // engine to sit on a segment running through the node — a train merely
        // passing close on a parallel track can't score.
        const auto& locoSeg = track.getSegment (p.pos.segment);
        if (locoSeg.nodeA != dz.node && locoSeg.nodeB != dz.node)
            continue;

        auto dzPos   = track.getNode (dz.node).position;
        auto locoPos = track.worldPos (p.pos);
        if (locoPos.getDistanceFrom (dzPos) >= kScoreDistance)
            continue;

        // Hand over every coupled car. (Coupled cars have no physics body, so
        // there's nothing to clean up beyond dropping them from the world.)
        int delivered = p.totalCars();
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

        // The whole consist arrives together as one shipment: build the streak so
        // the Nth car is worth N points (bigger consists score progressively
        // more), and sound the score cue once.
        bool firstOfShipment = (p.deliveryStreak == 0);
        for (int i = 0; i < delivered; ++i)
        {
            p.deliveryStreak++;
            p.score += p.deliveryStreak;
        }
        p.deliveryTimer = kDeliveryStreakWindow;
        if (firstOfShipment)
            soundEvents.push_back ({ SoundEvent::score, dzPos });

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

void GameState::updateWeather (float dt)
{
    // --- Wind: drift toward a fresh random target every so often. ---
    windChangeTimer -= dt;
    if (windChangeTimer <= 0.0f)
    {
        float curAngle = std::atan2 (wind.y, wind.x);
        float newAngle = curAngle + (rng().nextFloat() - 0.5f) * kWindAngleChangeMax * 2.0f;
        float newStr   = juce::jlimit (kWindMinStrength, 1.0f,
                                       wind.getDistanceFromOrigin()
                                       + (rng().nextFloat() - 0.5f) * kWindStrengthChangeMax);
        targetWind = { std::cos (newAngle) * newStr, std::sin (newAngle) * newStr };
        windChangeTimer = kWindChangeInterval;
    }
    wind += (targetWind - wind) * (kWindLerpSpeed * dt);

    // --- Smoke: each engine puffs from its stack; puffs fade, grow and drift. ---
    for (auto& p : players)
    {
        float speed01 = juce::jmin (1.0f, std::abs (p.speed) / kEngineTopSpeed);
        p.smokeAccum += (kSmokeIdleRate + speed01 * kSmokeSpeedRate) * dt;

        while (p.smokeAccum >= 1.0f && (int) smoke.size() < kSmokeMaxPuffs)
        {
            p.smokeAccum -= 1.0f;

            auto ew = track.worldPos (p.pos);
            float ang = track.trackAngle (p.pos, p.dir);
            juce::Point<float> at { ew.x + std::cos (ang) * kSmokeStackOffset
                                       + (rng().nextFloat() - 0.5f) * 0.12f,
                                    ew.y + std::sin (ang) * kSmokeStackOffset
                                       + (rng().nextFloat() - 0.5f) * 0.12f };

            SmokePuff s;
            s.pos      = at;
            s.radius   = kSmokeBaseRadius + rng().nextFloat() * kSmokeRadiusRand;
            s.alpha    = kSmokeBaseAlpha;
            float life = kSmokeFadeMin + rng().nextFloat() * (kSmokeFadeMax - kSmokeFadeMin);
            s.fadeRate = 1.0f / life;
            s.windAngleOffset = (rng().nextFloat() - 0.5f) * kSmokeWindAngleVariation;
            s.grey = p.boosting ? 0.16f : 0.42f;   // boosting engines belch darker smoke
            smoke.push_back (s);
        }
        p.smokeAccum = juce::jmin (p.smokeAccum, 1.0f);   // don't burst after a hitch
    }

    for (auto it = smoke.begin(); it != smoke.end();)
    {
        it->alpha  -= it->fadeRate * dt;
        it->radius += kSmokeGrowth * dt;

        // Disperse: rotate the wind by this puff's fixed offset so the plume
        // spreads instead of sliding as one rigid blob.
        float c  = std::cos (it->windAngleOffset);
        float sn = std::sin (it->windAngleOffset);
        juce::Point<float> drift { wind.x * c - wind.y * sn,
                                   wind.x * sn + wind.y * c };
        it->pos += drift * (kSmokeWindStrength * dt);

        if (it->alpha <= 0.0f)
            it = smoke.erase (it);
        else
            ++it;
    }
}

bool GameState::trainFoulsSwitch (const Player& p, int switchNode) const
{
    auto nodePos = track.getNode (switchNode).position;
    constexpr float kOccupyRadius = 1.2f;

    auto isNear = [&] (TrackPos tp) -> bool
    {
        const auto& seg = track.getSegment (tp.segment);
        if (seg.nodeA != switchNode && seg.nodeB != switchNode)
            return false;
        return track.worldPos (tp).getDistanceFrom (nodePos) < kOccupyRadius;
    };

    if (isNear (p.pos))
        return true;

    for (int i = 0; i < (int) p.frontCars.size(); ++i)
        if (isNear (track.advance (p.pos, p.dir, (float) (i + 1) * kCarSpacing).pos))
            return true;

    for (int i = 0; i < (int) p.rearCars.size(); ++i)
        if (isNear (track.advance (p.pos, -p.dir, (float) (i + 1) * kCarSpacing).pos))
            return true;

    return false;
}

bool GameState::isSwitchOccupied (int switchNode) const
{
    const auto* sw = track.findSwitch (switchNode);
    if (sw == nullptr) return false;

    for (const auto& p : players)
        if (trainFoulsSwitch (p, switchNode))
            return true;

    auto nodePos = track.getNode (switchNode).position;
    constexpr float kOccupyRadius = 1.2f;
    for (const auto& c : cars)
    {
        if (! c.free) continue;
        const auto& seg = track.getSegment (c.pos.segment);
        if (seg.nodeA != switchNode && seg.nodeB != switchNode) continue;
        if (track.worldPos (c.pos).getDistanceFrom (nodePos) < kOccupyRadius)
            return true;
    }

    return false;
}

GameState::SwitchTarget GameState::nextSwitchInTravel (const Player& p) const
{
    // Direction the leading end is actually moving (default to facing when
    // stopped). The next switch reached that way is the one lined up to throw.
    int travelDir = (p.speed < 0.0f) ? -p.dir : p.dir;
    int numLead   = (travelDir == p.dir) ? (int) p.frontCars.size()
                                         : (int) p.rearCars.size();
    auto endPos = track.advance (p.pos, travelDir, (float) numLead * kCarSpacing);

    auto node = track.nextSwitchAhead (endPos.pos, endPos.dir);
    if (! node.has_value())
        return {};

    float d = track.worldPos (endPos.pos).getDistanceFrom (track.getNode (*node).position);
    return { *node, d };
}

void GameState::updateSwitchTargets()
{
    std::vector<SwitchTarget> tgt (players.size());
    for (size_t i = 0; i < players.size(); ++i)
        tgt[i] = nextSwitchInTravel (players[(size_t) i]);

    for (size_t i = 0; i < players.size(); ++i)
    {
        auto& p = players[i];
        p.ringSwitch = -1;
        p.ringFlippable = false;

        int node = tgt[i].node;
        if (node < 0)
            continue;

        // A contested switch belongs to the closest train (ties: lower slot);
        // farther trains get no ring and can't throw it.
        bool owned = true;
        for (size_t j = 0; j < players.size(); ++j)
        {
            if (j == i || tgt[j].node != node) continue;
            if (tgt[j].dist < tgt[i].dist || (tgt[j].dist == tgt[i].dist && j < i))
                { owned = false; break; }
        }
        if (! owned)
            continue;

        p.ringSwitch = node;
        const auto* sw = track.findSwitch (node);
        p.ringFlippable = (sw != nullptr && sw->cooldown <= 0.0f && ! isSwitchOccupied (node));
    }
}

void GameState::flipSwitch (int switchNode)
{
    if (auto* sw = track.findSwitch (switchNode))
    {
        sw->reversed = ! sw->reversed;
        sw->cooldown = 0.5f;
        soundEvents.push_back ({ SoundEvent::points, track.getNode (sw->node).position });
    }
}

bool GameState::otherEngineInWindow (const Player& seeker, int segment, float lo, float hi) const
{
    auto inWin = [&] (TrackPos tp)
    {
        return tp.segment == segment && tp.distance >= lo && tp.distance <= hi;
    };

    for (const auto& other : players)
    {
        if (&other == &seeker) continue;

        if (inWin (other.pos))
            return true;

        for (int i = 0; i < (int) other.frontCars.size(); ++i)
            if (inWin (track.advance (other.pos, other.dir, (float) (i + 1) * kCarSpacing).pos))
                return true;

        for (int i = 0; i < (int) other.rearCars.size(); ++i)
            if (inWin (track.advance (other.pos, -other.dir, (float) (i + 1) * kCarSpacing).pos))
                return true;
    }

    return false;
}

bool GameState::engineBlocksTarget (const Player& seeker, TrackPos target) const
{
    // Same segment: an engine blocks only if it sits between us and the car.
    if (seeker.pos.segment == target.segment)
    {
        float lo = std::min (seeker.pos.distance, target.distance);
        float hi = std::max (seeker.pos.distance, target.distance);
        return otherEngineInWindow (seeker, seeker.pos.segment, lo, hi);
    }

    auto path = track.findPath (seeker.pos, seeker.dir, target).segments;
    if (path.empty())
        return false;   // no known route — let the normal logic deal with it

    auto sharedNode = [&] (int segA, int segB)
    {
        const auto& a = track.getSegment (segA);
        const auto& b = track.getSegment (segB);
        if (a.nodeA == b.nodeA || a.nodeA == b.nodeB) return a.nodeA;
        return a.nodeB;
    };

    for (int i = 0; i < (int) path.size(); ++i)
    {
        int seg = path[(size_t) i];
        const auto& s = track.getSegment (seg);

        // The stretch of this segment that lies on the route between us and
        // the car. The first segment runs from our position to the exit node;
        // the last runs from the entry node to the car; middle segments are
        // traversed end to end.
        float lo, hi;
        if (i == 0)
        {
            int node = sharedNode (seg, path[1]);
            float endDist = (s.nodeA == node) ? 0.0f : s.length;
            lo = std::min (seeker.pos.distance, endDist);
            hi = std::max (seeker.pos.distance, endDist);
        }
        else if (i == (int) path.size() - 1)
        {
            int node = sharedNode (seg, path[(size_t) i - 1]);
            float startDist = (s.nodeA == node) ? 0.0f : s.length;
            lo = std::min (startDist, target.distance);
            hi = std::max (startDist, target.distance);
        }
        else
        {
            lo = 0.0f;
            hi = s.length;
        }

        if (otherEngineInWindow (seeker, seg, lo, hi))
            return true;
    }

    return false;
}

bool GameState::canToggleSwitch (int switchNode) const
{
    const auto* sw = track.findSwitch (switchNode);
    if (sw == nullptr) return false;
    return sw->cooldown <= 0.0f && ! isSwitchOccupied (switchNode);
}

TrackPos GameState::carTrackPos (const Player& p, bool front, int index) const
{
    float dist = (float) (index + 1) * kCarSpacing;
    int walkDir = front ? p.dir : -p.dir;
    return track.advance (p.pos, walkDir, dist).pos;
}

juce::Point<float> GameState::carWorldPos (const Player& p, bool front, int index) const
{
    return track.worldPos (carTrackPos (p, front, index));
}

float GameState::panForWorldX (float worldX) const
{
    if (track.numNodes() == 0)
        return 0.0f;

    float minX = 1.0e9f, maxX = -1.0e9f;
    for (int i = 0; i < track.numNodes(); ++i)
    {
        float x = track.getNode (i).position.x;
        minX = juce::jmin (minX, x);
        maxX = juce::jmax (maxX, x);
    }

    float centre = (minX + maxX) * 0.5f;
    float halfW  = (maxX - minX) * 0.5f + 4.0f;   // pad matches the camera framing
    if (halfW <= 0.0f)
        return 0.0f;

    return juce::jlimit (-1.0f, 1.0f, (worldX - centre) / halfW);
}

// ============================================================================
// Diagnostic logging
// ============================================================================

void GameState::openLog (int numPlayers, int mapIndex)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                 .getChildFile ("Library/Logs/Shunt");
    dir.createDirectory();

    auto now   = juce::Time::getCurrentTime();
    auto stamp = now.formatted ("%Y%m%d-%H%M%S")
               + "-" + juce::String (now.getMilliseconds()).paddedLeft ('0', 3);
    auto file  = dir.getChildFile ("shunt-" + stamp + ".log");

    logStream = file.createOutputStream();
    if (logStream == nullptr)
        return;

    logStream->setPosition (0);
    logStream->truncate();

    const auto& maps = getMaps();
    juce::String mapName = (mapIndex >= 0 && mapIndex < (int) maps.size())
                             ? maps[(size_t) mapIndex].name : juce::String ("default-yard");

    logLine ("=== Shunt game log ===");
    logLine ("started " + now.toString (true, true));
    logLine ("map=" + mapName + "  players=" + juce::String (numPlayers)
             + "  cars=" + juce::String ((int) cars.size()));
    for (const auto& dz : track.getDropOffs())
        logLine ("dropoff colour=" + juce::String (dz.colourIndex)
                 + " node=" + juce::String (dz.node)
                 + " at=" + track.getNode (dz.node).position.toString());
    logLine ("columns: P<slot> <colour> <driver>:<aiState> spd=<speed> seg=<segment> d=<dist> dir=<facing>"
             " pos=(x,y) tgtCar=<id> tgt=(seg/dist) wait=<n> path=<len> stuck=<n> cars=F<n>/R<n>"
             " lock=<colour|-> score=<n>");
    logLine ("---");
}

void GameState::logLine (const juce::String& text)
{
    if (logStream == nullptr)
        return;
    logStream->writeText (text + juce::newLine, false, false, nullptr);
    logStream->flush();
}

void GameState::logSnapshot (bool includeCars)
{
    if (logStream == nullptr)
        return;

    auto t = juce::String (logClock, 2);

    for (const auto& p : players)
    {
        auto w = track.worldPos (p.pos);
        juce::String line;
        line << "[t=" << t << "] P" << p.slot << " ";

        if (p.ai.has_value())
        {
            const auto& b = *p.ai;
            line << colourName (p.carryColour) << " ai:" << aiStateName (b.state)
                 << " spd=" << juce::String (p.speed, 1)
                 << " seg=" << p.pos.segment << " d=" << juce::String (p.pos.distance, 1)
                 << " dir=" << p.dir
                 << " pos=(" << juce::String (w.x, 1) << "," << juce::String (w.y, 1) << ")"
                 << " tgtCar=" << b.targetCarId
                 << " tgt=" << b.targetPos.segment << "/" << juce::String (b.targetPos.distance, 1)
                 << " wait=" << b.waitCount << " path=" << (int) b.path.size()
                 << " stuck=" << b.stuckCount;
        }
        else
        {
            line << "human spd=" << juce::String (p.speed, 1)
                 << " seg=" << p.pos.segment << " d=" << juce::String (p.pos.distance, 1)
                 << " dir=" << p.dir
                 << " pos=(" << juce::String (w.x, 1) << "," << juce::String (w.y, 1) << ")";
        }

        line << " cars=F" << (int) p.frontCars.size() << "/R" << (int) p.rearCars.size()
             << " lock=" << (p.hasColourLock ? colourName (p.carryColour) : "-")
             << " score=" << p.score;

        // Coupled-car track positions, so a car jumping to another track (e.g.
        // shoved across a switch by a collision) shows up as a segment that
        // isn't adjacent to the engine's.
        if (p.totalCars() > 0)
        {
            line << " consist=[";
            for (int ci = 0; ci < (int) p.frontCars.size(); ++ci)
            {
                auto cp = carTrackPos (p, true, ci);
                line << "F" << cp.segment << "/" << juce::String (cp.distance, 1) << " ";
            }
            for (int ci = 0; ci < (int) p.rearCars.size(); ++ci)
            {
                auto cp = carTrackPos (p, false, ci);
                line << "R" << cp.segment << "/" << juce::String (cp.distance, 1) << " ";
            }
            line << "]";
        }
        logLine (line);
    }

    if (! includeCars)
        return;

    juce::String freeCars;
    int nFree = 0;
    for (const auto& c : cars)
    {
        if (! c.free) continue;
        ++nFree;
        freeCars << " #" << c.id << ":" << colourName (c.colour)
                 << " s" << c.pos.segment << "/" << juce::String (c.pos.distance, 1);
    }
    logLine ("[t=" + t + "] CARS free=" + juce::String (nFree) + ":" + freeCars);
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

bool GameState::aiWantsBoost (const Player& p) const
{
    if (! p.ai.has_value())
        return false;

    const auto& brain = *p.ai;

    // Boost is a rare, powerful burst (seconds to spend, far longer to refill),
    // so the AI only spends it on a clear run toward something worth reaching
    // faster: hauling a consist home to its drop-off, or racing empty to claim a
    // target car before a rival grabs it. Anything else — idling, leaving a
    // drop-off — lets the reserve recharge.
    if (brain.state == AiBrain::returningHome)
    {
        if (p.totalCars() == 0) return false;   // nothing to deliver
    }
    else if (brain.state == AiBrain::seekingCar)
    {
        if (brain.targetCarId < 0) return false;   // no car picked yet
    }
    else
    {
        return false;
    }

    // Don't burn boost while fighting for position — shoving, backing out of a
    // jam, or holding for a blocked drop-off. It'd just drain against a wall.
    if (brain.backoffTimer > 0.0f || brain.waiting) return false;
    if (brain.stuckCount > 0 || brain.noProgress > 0) return false;

    // Ease off near the goal so the speed ramp can brake and the engine stops
    // cleanly on the car / drop-off instead of overshooting at boosted speed.
    float distToGoal = track.worldPos (p.pos).getDistanceFrom (track.worldPos (brain.targetPos));
    if (distToGoal < kAiBoostEaseDist) return false;

    return true;
}

float GameState::aiWander (Player& p, bool rethink)
{
    auto& brain = *p.ai;

    if (brain.dirSign == 0)
        brain.dirSign = (p.dir >= 0) ? 1 : -1;

    // Turn around when we've stalled — i.e. run into a buffer/dead end and barely
    // moved since the last think.
    if (rethink)
    {
        auto cur = track.worldPos (p.pos);
        if (brain.lastPos.getDistanceFrom (cur) < 0.5f)
            brain.dirSign = -brain.dirSign;
        brain.lastPos = cur;
    }

    // Coin-flip the switch we're lined up on (and own) so we take a random branch.
    // Rate-limited via switchCooldown so we don't toggle it every frame.
    if (brain.switchCooldown <= 0.0f && p.ringSwitch >= 0 && p.ringFlippable)
    {
        if (rng().nextBool())
            flipSwitch (p.ringSwitch);
        brain.switchCooldown = 0.6f;
    }

    return (float) brain.dirSign * kTrainSpeed * 0.5f;
}

float GameState::aiUpdate (Player& p, float dt)
{
    if (! p.ai.has_value()) return 0.0f;
    auto& brain = *p.ai;

    brain.thinkTimer -= dt;
    brain.switchCooldown -= dt;
    brain.dirTimer -= dt;

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

    // Once we've coupled a car, head home. This must run before the seekingCar
    // logic below, which returns early when there are no free cars left to
    // target — otherwise coupling the last car leaves us stuck in seekingCar.
    if (brain.state == AiBrain::seekingCar && p.totalCars() > 0)
    {
        brain.state = AiBrain::returningHome;
        brain.targetCarId = -1;
        brain.path.clear();
    }

    // Leave drop-off: just drive forward until we've passed a switch
    if (brain.state == AiBrain::leavingDropOff)
    {
        // Check if we've reached a switch — safe to start seeking
        auto sw = track.nextSwitchAhead (p.pos, p.dir);
        auto swBack = track.nextSwitchAhead (p.pos, -p.dir);
        bool pastSwitch = false;
        if (sw.has_value())
        {
            auto swPos = track.getNode (*sw).position;
            if (track.worldPos (p.pos).getDistanceFrom (swPos) < 2.0f)
                pastSwitch = true;
        }
        // Also transition if stuck
        brain.stuckCount++;
        if (pastSwitch || brain.stuckCount > 30)
        {
            brain.state = AiBrain::idle;
            brain.stuckCount = 0;
        }
        return kTrainSpeed * 0.7f;
    }

    // Nothing left to collect and nothing to deliver — mill about at half speed.
    // Snap back to work the moment a car frees up (e.g. one gets rammed loose) or
    // we happen to roll over one and couple it.
    if (brain.state == AiBrain::wandering)
    {
        if (p.totalCars() > 0)
        {
            brain.state = AiBrain::returningHome;
        }
        else
        {
            bool anyFree = false;
            for (const auto& c : cars)
                if (c.free) { anyFree = true; break; }

            if (anyFree)
            {
                brain.state = AiBrain::seekingCar;
                brain.dirSign = 0;
            }
            else
                return aiWander (p, rethink);
        }
    }

    // Backing off to break a jam (usually against another engine): ease back
    // gently — a slow reverse, not a full-speed recoil — then re-path.
    if (brain.backoffTimer > 0.0f)
    {
        brain.backoffTimer -= dt;
        if (brain.backoffTimer > 0.0f)
            return (float) brain.backoffDir * kTrainSpeed * kBackoffSpeed;

        brain.path.clear();
        brain.targetCarId = -1;
        brain.dirSign = 0;
    }

    if (rethink)
    {
        auto curWorld = track.worldPos (p.pos);

        // Immobile: barely moved at all (wedged against a buffer or dead-stopped).
        if (brain.lastPos.getDistanceFrom (curWorld) < 0.5f)
            brain.stuckCount++;
        else
            brain.stuckCount = 0;
        brain.lastPos = curWorld;

        // No forward progress: moving, but not getting any closer to the goal —
        // this is the shoving-match case, where two engines push each other about
        // yet neither advances. Track the best distance we've reached and count
        // rethinks that fail to beat it. A big jump means the target changed (or
        // we were shoved far), so rebaseline instead of counting it.
        float distToTarget = curWorld.getDistanceFrom (track.worldPos (brain.targetPos));
        if (distToTarget < brain.bestDistToTarget - 0.5f
            || distToTarget > brain.bestDistToTarget + 4.0f)
        {
            brain.bestDistToTarget = distToTarget;
            brain.noProgress = 0;
        }
        else
            brain.noProgress++;

        float nearestEngine = 1.0e9f;
        for (const auto& other : players)
            if (&other != &p)
                nearestEngine = std::min (nearestEngine,
                                          curWorld.getDistanceFrom (track.worldPos (other.pos)));

        // A jam is either dead-stopped, or shoving another engine without
        // advancing. Either way, bail on the current plan.
        bool jammed = brain.stuckCount > 10 || brain.noProgress > 12;

        if (jammed)
        {
            // Is another engine actually in our way (nose-to-nose, or sitting on
            // the route ahead)? If so, yield by reversing out — don't gate this
            // on a tight distance, since two trains each holding a car measure
            // several units apart at the engines even while their cars touch.
            bool blockedOnPath = engineBlocksTarget (p, brain.targetPos);

            brain.stuckCount  = 0;
            brain.noProgress  = 0;
            brain.bestDistToTarget = 1.0e9f;
            brain.targetCarId = -1;
            brain.path.clear();

            if (nearestEngine < 6.0f || blockedOnPath)
            {
                // Stagger the reverse per player so two jammed engines don't back
                // off in lockstep and immediately re-deadlock.
                brain.backoffTimer = 1.0f + 0.4f * (float) p.slot;
                brain.backoffDir   = (brain.dirSign != 0) ? -brain.dirSign : -1;
                return (float) brain.backoffDir * kTrainSpeed * kBackoffSpeed;
            }
        }
    }

    // Find target
    juce::Point<float> targetWorld;
    bool hasTarget = false;

    if (brain.state == AiBrain::seekingCar)
    {
        // Give up on a target that another engine has moved in to block.
        if (rethink && brain.targetCarId >= 0 && engineBlocksTarget (p, brain.targetPos))
        {
            brain.targetCarId = -1;
            brain.path.clear();
        }

        if (rethink && brain.targetCarId < 0)
        {
            auto locoWorld = track.worldPos (p.pos);
            std::vector<std::pair<float, int>> unblocked, anyCar;

            for (const auto& c : cars)
            {
                if (! c.free) continue;
                if (p.hasColourLock && c.colour != p.carryColour)
                    continue;
                float d = locoWorld.getDistanceFrom (track.worldPos (c.pos));

                // Always a fallback candidate, even if an engine is in the way —
                // deferring to blockers forever is how the AI ends up idle.
                anyCar.push_back ({ d, c.id });

                if (! engineBlocksTarget (p, c.pos))   // another engine is in the way
                    unblocked.push_back ({ d, c.id });
            }

            // Prefer an unblocked car; fall back to a blocked one so we head
            // toward it and let the physics clear the standoff instead of idling.
            auto& candidates = ! unblocked.empty() ? unblocked : anyCar;
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

        if (! hasTarget)   // no free car anywhere — go mill about instead of idling
        {
            brain.state = AiBrain::wandering;
            brain.dirSign = 0;
            return aiWander (p, rethink);
        }
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
            return 0.0f;

        // If another engine is sitting on the way to (or in) the drop-off,
        // there's no alternative destination — hold position and wait for it to
        // clear rather than shoving in. But don't defer forever: an idle engine
        // (e.g. when we hold the last car) will never move, so time the wait out
        // and proceed, letting the physics nudge it aside.
        if (rethink)
        {
            constexpr int kMaxWaitRethinks = 10;   // ~2s at 0.2s per rethink
            bool blocked = engineBlocksTarget (p, brain.targetPos);
            brain.waitCount = blocked ? brain.waitCount + 1 : 0;
            brain.waiting = blocked && brain.waitCount < kMaxWaitRethinks;
        }

        if (brain.waiting)
            return 0.0f;
    }

    if (! hasTarget)
        return 0.0f;

    auto locoWorld = track.worldPos (p.pos);

    // Re-path when we reach a new segment or have no path
    bool segChanged = (p.pos.segment != brain.lastSeg);
    brain.lastSeg = p.pos.segment;

    if (brain.path.empty() || segChanged)
    {
        auto result = track.findPath (p.pos, p.dir, brain.targetPos);
        brain.path = std::move (result.segments);
        brain.pathDir = result.firstDir;
    }

    if (brain.path.empty())
        return 0.0f;

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

        // Runaround: reaching the next segment means crossing the switch at the
        // shared node. If that switch isn't routed our way yet and our own
        // train is fouling it, we can't throw it from here — so pull the other
        // way to clear the switch first, then flip it, then come back through.
        // ("go all the way past the switch, flip it, then reverse in.")
        int exitNode = (moveDir == 1) ? curS.nodeB : curS.nodeA;
        if (const auto* sw = track.findSwitch (exitNode))
        {
            int route = -1;
            if (p.pos.segment == sw->stemSegment)
                route = sw->reversed ? sw->reverseSegment : sw->normalSegment;
            else if (p.pos.segment == sw->normalSegment || p.pos.segment == sw->reverseSegment)
                route = sw->stemSegment;

            if (route != nextSeg && trainFoulsSwitch (p, exitNode))
                moveDir = -moveDir;   // pull away to clear the switch
        }
    }
    else
    {
        // On the target segment — go toward the target position
        moveDir = (brain.targetPos.distance > p.pos.distance) ? 1 : -1;
    }

    float throttle = (moveDir == p.dir) ? 1.0f : -1.0f;

    // Throw the next switch on the path so the route is clear. Like a human
    // player, the AI may only work the switch it is about to reach — not
    // switches further down the route.
    if (brain.switchCooldown <= 0.0f && brain.path.size() >= 2)
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
            if (sw == nullptr) continue;   // plain junction/crossing — look ahead

            // This is the next switch on the route. Whether or not we manage
            // to throw it, stop here: the AI can't reach past it to switches
            // further along. It may only throw the switch it's lined up on (its
            // ring) and owns — same rule the players play by.
            if (sharedNode == p.ringSwitch && p.ringFlippable)
            {
                int currentRoute = -1;
                if (segA == sw->stemSegment)
                    currentRoute = sw->reversed ? sw->reverseSegment : sw->normalSegment;
                else if (segA == sw->normalSegment)
                    currentRoute = sw->stemSegment;
                else if (segA == sw->reverseSegment)
                    currentRoute = sw->stemSegment;

                if (currentRoute != segB)
                {
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
                        soundEvents.push_back ({ SoundEvent::points,
                                                 track.getNode (sw->node).position });
                    }
                    else
                    {
                        sw->reversed = ! sw->reversed; // revert
                    }
                }
            }

            break;
        }
    }

    // Delivered everything — leave the drop-off before seeking again. (The
    // seekingCar -> returningHome transition is handled at the top, so it fires
    // even when no free cars remain to target.)
    if (brain.state == AiBrain::returningHome && p.totalCars() == 0)
    {
        brain.state = AiBrain::leavingDropOff;
        brain.path.clear();
        brain.stuckCount = 0;
    }

    // Rate-limit direction changes so the AI can't hunt back and forth on a
    // switch: once it commits to a direction it holds it for at least 0.5s.
    int desiredDir = (throttle >= 0.0f) ? 1 : -1;
    if (brain.dirSign == 0)
    {
        brain.dirSign  = desiredDir;
        brain.dirTimer = 0.5f;
    }
    else if (desiredDir != brain.dirSign)
    {
        if (brain.dirTimer > 0.0f)
            desiredDir = brain.dirSign;   // too soon — keep going the same way
        else
        {
            brain.dirSign  = desiredDir;
            brain.dirTimer = 0.5f;
        }
    }

    return (float) desiredDir * kTrainSpeed * 0.7f;
}

} // namespace game
