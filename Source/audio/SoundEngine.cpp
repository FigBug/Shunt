#include "SoundEngine.h"
#include "BinaryData.h"

namespace audio
{

SoundEngine::SoundEngine()
{
    formatManager.registerBasicFormats();

    // The source recording is a long single honk; take just the opening and
    // fade the tail so a button tap plays a punchy horn blast.
    loadSound (SoundID::horn, BinaryData::train_horn_ogg, BinaryData::train_horn_oggSize,
               3.0f /*maxSeconds*/, 0.4f /*fadeOut*/);

    auto err = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (err.isNotEmpty())
        DBG ("Audio init error: " + err);

    player.setSource (&mixer);
    deviceManager.addAudioCallback (&player);
}

SoundEngine::~SoundEngine()
{
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
}

void SoundEngine::loadSound (SoundID id, const void* data, int size,
                             float maxSeconds, float fadeOutSeconds)
{
    auto stream = std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false);
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (std::move (stream)));

    if (reader == nullptr)
        return;

    auto sr = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    int total = (int) reader->lengthInSamples;
    int wanted = maxSeconds > 0.0f ? juce::jmin (total, (int) (maxSeconds * sr)) : total;

    auto& buf = buffers[(size_t) id];
    buf.setSize ((int) reader->numChannels, wanted);
    reader->read (&buf, 0, wanted, 0, true, true);

    // Small fade-in kills the initial click; fade-out smooths the trimmed tail.
    int fadeIn = juce::jmin (wanted, (int) (0.005 * sr));
    if (fadeIn > 1)
        buf.applyGainRamp (0, fadeIn, 0.0f, 1.0f);

    int fadeOut = juce::jlimit (0, wanted, (int) (fadeOutSeconds * sr));
    if (fadeOut > 1)
        buf.applyGainRamp (wanted - fadeOut, fadeOut, 1.0f, 0.0f);
}

void SoundEngine::play (SoundID id, float gain, float pan)
{
    auto& buf = buffers[(size_t) id];
    if (buf.getNumSamples() == 0)
        return;

    mixer.volume.store (masterVolume);

    juce::SpinLock::ScopedLockType sl (mixer.lock);
    for (auto& v : mixer.voices)
    {
        if (! v.active)
        {
            v.buffer   = &buf;
            v.position = 0;
            v.gain     = gain;
            v.pan      = juce::jlimit (-1.0f, 1.0f, pan);
            v.active   = true;
            return;
        }
    }
}

void SoundEngine::setEngine (int index, bool active, float speed01, float pan)
{
    if (index < 0 || index >= kMaxEngines)
        return;

    mixer.volume.store (masterVolume);

    auto& e = mixer.engines[(size_t) index];
    speed01 = juce::jlimit (0.0f, 1.0f, speed01);

    // Idle rumble even when stopped; pitch and level climb with speed.
    e.active.store (active);
    e.targetGain.store (active ? 0.05f + 0.16f * speed01 : 0.0f);
    e.targetFreq.store (38.0f + 60.0f * speed01);
    e.pan.store (juce::jlimit (-1.0f, 1.0f, pan));
}

void SoundEngine::stopAllEngines()
{
    for (auto& e : mixer.engines)
    {
        e.active.store (false);
        e.targetGain.store (0.0f);
    }
}

void SoundEngine::Mixer::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    const float vol = volume.load();
    const int   n   = info.numSamples;
    const int   outCh = info.buffer->getNumChannels();

    // ---- sample voices ----
    {
        juce::SpinLock::ScopedLockType sl (lock);
        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            int remaining = v.buffer->getNumSamples() - v.position;
            int toWrite   = juce::jmin (n, remaining);
            int srcCh     = v.buffer->getNumChannels();

            float lg = v.gain * vol * juce::jmin (1.0f, 1.0f - v.pan);
            float rg = v.gain * vol * juce::jmin (1.0f, 1.0f + v.pan);

            if (outCh >= 2)
            {
                info.buffer->addFrom (0, info.startSample, *v.buffer, 0, v.position, toWrite, lg);
                info.buffer->addFrom (1, info.startSample, *v.buffer, juce::jmin (1, srcCh - 1), v.position, toWrite, rg);
            }
            else if (outCh == 1)
            {
                info.buffer->addFrom (0, info.startSample, *v.buffer, 0, v.position, toWrite, v.gain * vol);
            }

            v.position += toWrite;
            if (v.position >= v.buffer->getNumSamples())
                v.active = false;
        }
    }

    // ---- synthesised engine voices ----
    const float sr        = (float) sampleRate;
    const float twoPi     = juce::MathConstants<float>::twoPi;
    const float gainSmooth = 1.0f - std::exp (-1.0f / (0.05f * sr));   // ~50ms
    const float freqSmooth = 1.0f - std::exp (-1.0f / (0.08f * sr));   // ~80ms

    float* outL = outCh > 0 ? info.buffer->getWritePointer (0, info.startSample) : nullptr;
    float* outR = outCh > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;

    for (auto& e : engines)
    {
        float tg = e.targetGain.load();
        if (tg <= 0.0f && e.curGain < 1.0e-4f)
            continue;   // silent and staying silent — skip

        float tf  = e.targetFreq.load();
        float pan = e.pan.load();
        float lg  = vol * juce::jmin (1.0f, 1.0f - pan);
        float rg  = vol * juce::jmin (1.0f, 1.0f + pan);

        for (int i = 0; i < n; ++i)
        {
            e.curGain += (tg - e.curGain) * gainSmooth;
            e.curFreq += (tf - e.curFreq) * freqSmooth;

            e.phase    += twoPi * e.curFreq / sr;
            e.phaseSub += twoPi * (e.curFreq * 0.5f) / sr;
            if (e.phase    >= twoPi) e.phase    -= twoPi;
            if (e.phaseSub >= twoPi) e.phaseSub -= twoPi;

            // Body: sub-octave sine + fundamental + a buzzy 2nd harmonic.
            float body = 0.55f * std::sin (e.phaseSub)
                       + 0.35f * std::sin (e.phase)
                       + 0.18f * std::sin (2.0f * e.phase);

            // Grit: white noise through a one-pole lowpass, a touch brighter
            // with speed. xorshift keeps it cheap and lock-free.
            e.rngState ^= e.rngState << 13;
            e.rngState ^= e.rngState >> 17;
            e.rngState ^= e.rngState << 5;
            float white = ((float) e.rngState / (float) 0xffffffffu) * 2.0f - 1.0f;
            float noiseCut = juce::jlimit (0.02f, 0.5f, e.curFreq / sr * 8.0f);
            e.noiseLP += noiseCut * (white - e.noiseLP);

            float s = (body + 0.25f * e.noiseLP) * e.curGain;

            if (outL) outL[i] += s * lg;
            if (outR) outR[i] += s * rg;
        }
    }
}

} // namespace audio
