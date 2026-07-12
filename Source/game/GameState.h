#pragma once

#include "Track.h"
#include "Player.h"
#include "Physics.h"
#include <vector>

namespace gin { class GameControllerManager; }

namespace game
{

// A drifting puff of engine smoke. Ported from Heligoland's funnel smoke —
// each puff carries a fixed random wind-angle offset so a plume disperses
// rather than moving as one rigid blob.
struct SmokePuff
{
    juce::Point<float> pos;      // world position
    float radius = 0.2f;
    float alpha = 1.0f;          // 0..1
    float fadeRate = 0.5f;       // alpha lost per second
    float windAngleOffset = 0.0f;
    float grey = 0.42f;          // 0..1 sootiness; darker when the engine boosts
};

class GameState
{
public:
    explicit GameState (int numPlayers, int mapIndex = 0);

    void update (float dt, gin::GameControllerManager& controllers);

    // Fire-and-forget audio cues raised during update(), drained by the host
    // each tick (mirrors the Provins pattern). worldPos allows future panning.
    struct SoundEvent
    {
        enum Type { horn, couple, uncouple, collision, score, points };
        Type               type;
        juce::Point<float> worldPos;
    };
    const std::vector<SoundEvent>& getSoundEvents() const noexcept { return soundEvents; }

    const TrackGraph&          getTrack()   const noexcept { return track; }
    TrackGraph&                getTrack()         noexcept { return track; }
    const std::vector<Player>& getPlayers() const noexcept { return players; }
    const std::vector<Car>&    getCars()    const noexcept { return cars; }

    // Engine smoke (rendered by GameView) and the wind vector driving it
    // (length = strength 0..1, direction = heading). No wind UI is drawn.
    const std::vector<SmokePuff>& getSmoke() const noexcept { return smoke; }
    juce::Point<float>            getWind()  const noexcept { return wind; }

    bool  isGameOver()       const noexcept { return gameOver; }

    void spawnPlayer (int controllerIndex, int slot);
    void spawnAi (int slot);

    TrackPos           carTrackPos (const Player& p, bool front, int index) const;
    juce::Point<float> carWorldPos (const Player& p, bool front, int index) const;
    float              carAngle    (const Player& p, bool front, int index) const;

    // Stereo pan in [-1, 1] for a world X, mapped across the track's horizontal
    // extent (matches the on-screen camera framing). Used to place sounds.
    float              panForWorldX (float worldX) const;

    bool canToggleSwitch (int switchNode) const;

private:
    void  handleActions   (Player& p, float engineSpeed, bool flip, bool uncouple);
    void  decoupleAll     (Player& p, float engineSpeed);
    // Ram-stealing: a hard train-to-train hit shears the outermost car off the
    // end that was struck. playerForBody maps a physics body back to its engine;
    // tryRamBreak works out which end took the hit and, if a car sits there,
    // breakOffCar sets it loose at that spot for anyone to grab.
    Player* playerForBody (int bodyId);
    void  tryRamBreak     (Player& victim, juce::Point<float> hit);
    void  breakOffCar     (Player& victim, bool front);
    void  flipSwitch      (int switchNode);   // toggle + cooldown + sound cue

    // The next switch a train will reach in its current direction of travel,
    // and the distance to it (used to resolve who owns a contested switch).
    struct SwitchTarget { int node = -1; float dist = 0.0f; };
    SwitchTarget nextSwitchInTravel (const Player& p) const;
    // Each frame: work out which switch each train is lined up on, who owns a
    // contested one (the closer train), and whether it can be thrown.
    void  updateSwitchTargets();
    float aiUpdate        (Player& p, float dt);
    void  checkCoupling   (Player& p);
    void  checkScoring    (Player& p);
    void  placeInitialCars();
    void  placeCarsFromSpawns();   // used when the level defines explicit spawns
    // Distance along the track from `start` in `dir` to the nearest switch,
    // capped at `limit`. Used to reject spawn points that sit on top of a switch.
    float distanceToSwitch (TrackPos start, int dir, float limit) const;
    struct SpawnInfo { TrackPos pos; int dir; };
    SpawnInfo spawnPosNearDropOff (int dropOffIndex) const;
    bool  isSwitchOccupied (int switchNode) const;
    // True when this player's own engine or cars foul the switch node.
    bool  trainFoulsSwitch (const Player& p, int switchNode) const;
    // True when any engine other than seeker (or its coupled cars) sits within
    // [lo, hi] on the given segment.
    bool  otherEngineInWindow (const Player& seeker, int segment, float lo, float hi) const;
    // True when another engine sits on the route from seeker to target, i.e.
    // between the AI and the car it wants — a reason to give up on that car.
    bool  engineBlocksTarget (const Player& seeker, TrackPos target) const;
    void  updateWeather (float dt);   // advances the wind and engine-smoke sim

    // Per-game diagnostic log: train/car positions and AI state, one file per
    // game under ~/Library/Logs/Shunt. Read it after a play-test to see what the
    // AI was thinking when something went wrong.
    void  openLog     (int numPlayers, int mapIndex);
    void  logLine     (const juce::String& text);
    void  logSnapshot (bool includeCars);
    std::unique_ptr<juce::FileOutputStream> logStream;
    double logClock = 0.0;    // seconds since game start
    float  logTimer = 0.0f;   // time since the last snapshot
    float  logCarsTimer = 0.0f;   // time since the last free-car dump

    TrackGraph           track;
    PhysicsEngine        physics;
    std::vector<Player>  players;
    std::vector<Car>     cars;
    std::vector<SoundEvent> soundEvents;
    std::vector<int>     spawnDropOffOrder;
    int                  nextCarId   = 0;
    bool                 gameOver    = false;

    std::vector<SmokePuff> smoke;
    juce::Point<float>   wind, targetWind;   // length = strength 0..1
    float                windChangeTimer = 0.0f;

    static constexpr float kTrainSpeed    = kEngineTopSpeed;
    static constexpr float kCarSpacing    = 1.6f;
    static constexpr float kVehicleHalfLen = 0.8f;
    static constexpr float kEngineMass    = 3.0f;
    static constexpr float kCarMass       = 1.0f;
    static constexpr float kCarFriction   = 2.0f;
    // Tractive / braking forces. Acceleration = force / mass, so heavier
    // consists build and shed speed more slowly. Powering under throttle,
    // coasting with no throttle, and braking with reverse throttle each use a
    // different force: coast < power < brake.
    static constexpr float kEngineForce   = 24.0f;
    static constexpr float kCoastForce    = 6.0f;
    static constexpr float kBrakeForce    = 36.0f;
    static constexpr float kCoupleDistance = 0.5f;
    static constexpr float kScoreDistance  = 2.5f;
    // Cars delivered within this window of each other count as one run, so the
    // whole consist scores as a scaling combo.
    static constexpr float kDeliveryStreakWindow = 1.5f;
    static constexpr int   kMaxConsist     = 100;
    // Ram-stealing: a train-to-train hit closing faster than this shears the
    // struck consist's outermost car loose; kRamCooldown then gates that
    // consist so one collision peels at most one car.
    static constexpr float kRamBreakSpeed  = 4.0f;
    static constexpr float kRamCooldown    = 1.0f;
    static constexpr int   kInitialCars    = 20;
    static constexpr float kBackoffSpeed   = 0.35f;  // gentle jam-escape reverse (× top speed)

    // Boost: the horn spends the reserve for a higher top speed. Empties in
    // kBoostEmptyTime, refills over kBoostFillTime.
    static constexpr float kBoostFillTime  = 25.0f;
    static constexpr float kBoostEmptyTime = 2.5f;
    static constexpr float kBoostSpeedMult = 1.5f;

    // Wind (ported from Heligoland). The vector's length is strength 0..1; it
    // drifts toward a fresh random target every kWindChangeInterval seconds.
    static constexpr float kWindChangeInterval   = 18.0f;
    static constexpr float kWindLerpSpeed        = 0.12f;
    static constexpr float kWindMinStrength      = 0.30f;
    static constexpr float kWindAngleChangeMax   = 0.70f;   // radians per gust
    static constexpr float kWindStrengthChangeMax = 0.40f;

    // Engine smoke, tuned for the track's world scale (a loco is ~1.4 wide).
    static constexpr float kSmokeFadeMin     = 2.2f;   // puff lifetime range (s)
    static constexpr float kSmokeFadeMax     = 4.0f;
    static constexpr float kSmokeWindStrength = 0.7f;  // world units/s per unit wind
    static constexpr float kSmokeIdleRate    = 20.0f;  // puffs/s when stopped
    static constexpr float kSmokeSpeedRate   = 64.0f;  // extra puffs/s at full speed
    static constexpr float kSmokeBaseRadius  = 0.075f;
    static constexpr float kSmokeRadiusRand  = 0.05f;
    static constexpr float kSmokeGrowth      = 0.08f;  // radius gained per second
    static constexpr float kSmokeBaseAlpha   = 0.5f;
    static constexpr float kSmokeWindAngleVariation = 0.5f;  // ± half this, radians
    static constexpr float kSmokeStackOffset = 0.22f;  // stack ahead of engine centre
    static constexpr int   kSmokeMaxPuffs    = 2800;
};

} // namespace game
