#include "audio.h"

#include "miniaudio.h"

#include <cstdio>

// The one device + mixing graph for the whole process. File-static rather than
// extern-in-the-header on purpose — see the note at the top of audio.h.
static ma_engine engine;
static bool ready = false;

// A fixed pool of one-shot voices.
//
// miniaudio has its own fire-and-forget call, ma_engine_play_sound(), but it
// hands back nothing — so there is no way to set a per-sound volume with it.
// Owning the ma_sound objects ourselves is what buys `volume`, and it is the
// same pool AudioSource will hang off in Phase 3.
//
// Fixed size, never grown: allocating in response to gameplay is how you get a
// frame hitch, and 32 simultaneous one-shots is far more than a scene this size
// will ask for. Past that, new sounds are dropped rather than stealing a
// playing voice — a gunshot that never starts is less noticeable than one that
// cuts off halfway.
static const int MAX_VOICES = 32;

struct Voice
{
    ma_sound sound;
    bool active = false;
};

static Voice voices[MAX_VOICES];

// Frees any voice that has finished playing.
//
// Called from playSound2D() rather than on a timer, so a frame that plays
// nothing costs nothing. This must stay on the main thread: ma_sound_uninit()
// tears down a node that the audio thread is walking, and miniaudio only
// guarantees that is safe from outside the callback.
static void reclaimVoices()
{
    for (int i = 0; i < MAX_VOICES; i++)
    {
        if (voices[i].active && ma_sound_at_end(&voices[i].sound))
        {
            ma_sound_uninit(&voices[i].sound);
            voices[i].active = false;
        }
    }
}

int initAudio()
{
    if (ready) return 0;

    // A null config takes miniaudio's defaults: the OS default playback device,
    // its native sample rate, and a resource manager that caches decoded files
    // by path — which is why playing the same clip twice only decodes it once.
    ma_result result = ma_engine_init(NULL, &engine);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "initAudio: ma_engine_init failed: %s\n",
                ma_result_description(result));
        return (int)result;
    }

    ready = true;

    // Worth printing: "no sound" and "sound going to the wrong device" look
    // identical from the player's chair, and this separates them immediately.
    // Note stderr is block-buffered when redirected on this MinGW setup, so
    // read this in a real terminal — the render loop never exits to flush it.
    ma_device* device = ma_engine_get_device(&engine);
    if (device != NULL)
    {
        fprintf(stderr, "audio: %s | %s | %u Hz | %u ch | %d voices\n",
                ma_get_backend_name(device->pContext->backend),
                device->playback.name,
                ma_engine_get_sample_rate(&engine),
                device->playback.channels,
                MAX_VOICES);
    }

    return 0;
}

void shutdownAudio()
{
    if (!ready) return;

    // Uninit the voices before the engine. They are nodes in the engine's graph,
    // and tearing the graph down underneath them would leave ma_sound_uninit()
    // walking freed memory.
    for (int i = 0; i < MAX_VOICES; i++)
    {
        if (voices[i].active)
        {
            ma_sound_uninit(&voices[i].sound);
            voices[i].active = false;
        }
    }

    ma_engine_uninit(&engine);
    ready = false;
}

bool audioReady()
{
    return ready;
}

bool playSound2D(const char* path, float volume)
{
    if (!ready || path == NULL) return false;

    reclaimVoices();

    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++)
    {
        if (!voices[i].active) { slot = i; break; }
    }

    if (slot < 0)
    {
        fprintf(stderr, "playSound2D: all %d voices busy, dropped '%s'\n",
                MAX_VOICES, path);
        return false;
    }

    // DECODE pulls the whole file into memory up front instead of decoding it
    // on the audio thread — right for short SFX, wrong for music (that wants
    // MA_SOUND_FLAG_STREAM, which Phase 2 splits out).
    //
    // NO_SPATIALIZATION is what makes this 2D. Without it miniaudio would
    // position the sound at the origin and attenuate it against the listener,
    // so a UI click would get quieter as the player walked away from world zero.
    ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;

    ma_result result = ma_sound_init_from_file(&engine, path, flags,
                                               NULL, NULL, &voices[slot].sound);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "playSound2D: could not load '%s': %s\n",
                path, ma_result_description(result));
        return false;
    }

    ma_sound_set_volume(&voices[slot].sound, volume);

    result = ma_sound_start(&voices[slot].sound);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "playSound2D: could not start '%s': %s\n",
                path, ma_result_description(result));
        ma_sound_uninit(&voices[slot].sound);
        return false;
    }

    voices[slot].active = true;
    return true;
}

void setMasterVolume(float volume)
{
    if (!ready) return;
    ma_engine_set_volume(&engine, volume);
}

int activeVoiceCount()
{
    if (!ready) return 0;

    // Reclaim first, or this reports voices that finished several seconds ago
    // and were simply never swept — nothing else calls reclaimVoices() unless a
    // new sound is played.
    reclaimVoices();

    int count = 0;
    for (int i = 0; i < MAX_VOICES; i++)
    {
        if (voices[i].active) count++;
    }
    return count;
}
