#include "SoundEngine.h"
#include "BinaryData.h"
#include <gin_dsp/gin_dsp.h>

namespace audio
{

SoundEngine::SoundEngine()
{
    formatManager.registerBasicFormats();

    auto err = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (err.isNotEmpty())
        DBG ("Audio init error: " + err);

    if (auto* dev = deviceManager.getCurrentAudioDevice())
        deviceSampleRate = dev->getCurrentSampleRate();

    // Each cue plays the whole sound file from Assets/sfx/ (any format),
    // resampled to the output rate so it plays at the right pitch.
    loadSound (SoundID::horn,      "train_horn");
    loadSound (SoundID::couple,    "couple");
    loadSound (SoundID::uncouple,  "uncouple");
    loadSound (SoundID::collision, "collision");
    loadSound (SoundID::score,     "score");
    loadSound (SoundID::points,    "points");

    // Precompute the horn's sustain loop: the centre 50% of the file, with a
    // ~30ms crossfade (capped to half the loop) to hide the wrap-around click.
    {
        const auto& hb = buffers[(size_t) SoundID::horn];
        int len = hb.getNumSamples();
        mixer.hornBuf = &hb;
        mixer.hornLen = len;
        mixer.hornA   = len / 4;
        mixer.hornB   = (len * 3) / 4;
        int loopLen   = mixer.hornB - mixer.hornA;
        mixer.hornXf  = juce::jlimit (1, juce::jmax (1, loopLen / 2),
                                      (int) (deviceSampleRate * 0.03));
    }

    player.setSource (&mixer);
    deviceManager.addAudioCallback (&player);
}

void SoundEngine::setHorn (int index, bool held, float pan)
{
    if (index < 0 || index >= kMaxEngines)
        return;
    mixer.horns[(size_t) index].held.store (held);
    mixer.horns[(size_t) index].pan.store (pan);
}

SoundEngine::~SoundEngine()
{
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
}

void SoundEngine::loadSound (SoundID id, const juce::String& baseName)
{
    // Find the baked resource whose original filename is "<baseName>.<ext>".
    const char* data = nullptr;
    int size = 0;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        juce::String orig (BinaryData::originalFilenames[i]);
        if (orig.upToLastOccurrenceOf (".", false, false) == baseName)
        {
            data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            break;
        }
    }
    if (data == nullptr)
        return;

    auto stream = std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false);
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (std::move (stream)));
    if (reader == nullptr)
        return;

    int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> raw ((int) reader->numChannels, len);
    reader->read (&raw, 0, len, 0, true, true);

    double fileRate = reader->sampleRate > 0 ? reader->sampleRate : deviceSampleRate;
    buffers[(size_t) id] = (std::abs (fileRate - deviceSampleRate) > 1.0)
        ? gin::resampleBuffer (raw, fileRate, deviceSampleRate)
        : std::move (raw);
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

    // ---- held horn voices: attack, crossfaded sustain loop, then release ----
    if (hornBuf != nullptr && hornLen > 4)
    {
        const float halfPi = juce::MathConstants<float>::halfPi;
        const int   srcCh  = hornBuf->getNumChannels();
        const float* d0    = hornBuf->getReadPointer (0);
        const float* d1    = hornBuf->getReadPointer (srcCh > 1 ? 1 : 0);
        constexpr float hornGain = 0.85f;

        for (auto& h : horns)
        {
            bool held = h.held.load();
            if (held && ! h.active) { h.active = true; h.pos = 0; h.looping = true; }
            else if (held)          { h.looping = true;  }   // keep looping while held
            else                    { h.looping = false; }   // released: play out to end
            if (! h.active)
                continue;

            float pan = h.pan.load();
            float lg  = hornGain * vol * juce::jmin (1.0f, 1.0f - pan);
            float rg  = hornGain * vol * juce::jmin (1.0f, 1.0f + pan);

            for (int s = 0; s < n && h.active; ++s)
            {
                if (h.looping && h.pos >= hornB)   // safety (e.g. re-press in the tail)
                    h.pos = hornA;

                float g1 = 1.0f, g2 = 0.0f;
                int   idx2 = h.pos;
                bool  inXf = h.looping && h.pos >= hornB - hornXf && h.pos < hornB;
                if (inXf)
                {
                    float t = (float) (h.pos - (hornB - hornXf)) / (float) hornXf;
                    g1 = std::cos (t * halfPi);   // equal-power crossfade
                    g2 = std::sin (t * halfPi);
                    idx2 = hornA + (h.pos - (hornB - hornXf));
                }

                float sL = d0[h.pos] * g1 + (inXf ? d0[idx2] * g2 : 0.0f);
                float sR = d1[h.pos] * g1 + (inXf ? d1[idx2] * g2 : 0.0f);

                if (outL) outL[s] += sL * lg;
                if (outR) outR[s] += sR * rg;

                ++h.pos;
                if (h.looping)
                {
                    if (h.pos >= hornB) h.pos = hornA + hornXf;   // wrap after crossfade
                }
                else if (h.pos >= hornLen)
                {
                    h.active = false;   // release finished
                }
            }
        }
    }

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
