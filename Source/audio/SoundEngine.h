#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>

namespace audio
{

// One-shot sample sounds. Add more here (coupling clank, delivery chime, ...)
// and load them in the constructor.
enum class SoundID
{
    horn,
    couple,
    uncouple,
    collision,
    score,
    points,     // a switch being thrown
    count
};

// Audio for Shunt. Sample playback follows the Provins SoundBank pattern (a
// small polyphonic voice mixer fed from BinaryData), and adds a set of
// synthesised diesel-engine voices — one per player — whose pitch and volume
// track each engine's speed in real time.
class SoundEngine
{
public:
    SoundEngine();
    ~SoundEngine();

    // Trigger a one-shot sample (horn, etc.).
    void play (SoundID id, float gain = 1.0f, float pan = 0.0f);

    static constexpr int kMaxEngines = 4;

    // Called every game tick: drive one synthesised engine. speed01 is the
    // engine's speed normalised to roughly [0, 1]. Inactive voices fade to
    // silence, so returning to the menu just calls this with active = false.
    void setEngine (int index, bool active, float speed01, float pan = 0.0f);
    void stopAllEngines();

    // Held-horn: starts on the first held==true, loops the centre 50% of the
    // horn file (crossfaded) while held, then plays out to the end on release.
    void setHorn (int index, bool held, float pan = 0.0f);

    void  setVolume (float v) noexcept { masterVolume = juce::jlimit (0.0f, 1.0f, v); }
    float getVolume() const noexcept   { return masterVolume; }

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer  player;

    std::array<juce::AudioBuffer<float>, (size_t) SoundID::count> buffers;
    double deviceSampleRate = 44100.0;   // output rate; loaded sounds resample to it
    float masterVolume = 0.8f;

    // ---- sample playback (Provins pattern) ----
    struct Voice
    {
        juce::AudioBuffer<float>* buffer = nullptr;
        int   position = 0;
        float gain = 1.0f;
        float pan  = 0.0f;
        bool  active = false;
    };
    static constexpr int kMaxVoices = 16;

    // ---- synthesised engine voice ----
    struct EngineVoice
    {
        std::atomic<bool>  active     { false };
        std::atomic<float> targetGain { 0.0f };  // set from speed by the game thread
        std::atomic<float> targetFreq { 0.0f };
        std::atomic<float> pan        { 0.0f };

        // audio-thread-only state
        float phase   = 0.0f;
        float phaseSub = 0.0f;
        float curGain = 0.0f;
        float curFreq = 40.0f;
        float noiseLP = 0.0f;
        juce::uint32 rngState = 0x9e3779b9u;
    };

    class Mixer : public juce::AudioSource
    {
    public:
        void prepareToPlay (int, double sr) override { sampleRate = sr > 0 ? sr : 44100.0; }
        void releaseResources() override {}
        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

        double sampleRate = 44100.0;

        std::array<Voice, kMaxVoices> voices {};
        juce::SpinLock lock;                 // guards sample-voice allocation
        std::atomic<float> volume { 1.0f };

        std::array<EngineVoice, kMaxEngines> engines {};

        // ---- held horn voice: attack → crossfaded sustain loop → release ----
        struct HornVoice
        {
            std::atomic<bool>  held { false };
            std::atomic<float> pan  { 0.0f };
            // audio-thread-only state
            bool active  = false;
            bool looping = false;
            int  pos     = 0;
        };
        std::array<HornVoice, kMaxEngines> horns {};

        const juce::AudioBuffer<float>* hornBuf = nullptr;
        int hornLen = 0, hornA = 0, hornB = 0, hornXf = 0;   // loop bounds + crossfade
    };

    Mixer mixer;

    // Load the whole baked sound file matching `baseName` (any extension) into
    // the buffer for `id`, ready to play as-is.
    void loadSound (SoundID id, const juce::String& baseName);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEngine)
};

} // namespace audio
