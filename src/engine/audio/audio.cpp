#include "audio.h"

#include "miniaudio.h"

#include <cstdio>

// The one device + mixing graph for the whole process. File-static rather than
// extern-in-the-header on purpose — see the note at the top of audio.h.
static ma_engine engine;
static bool ready = false;

// Where the ears were as of the last updateAudio().
//
// Kept on this side rather than read back out of the engine so the occlusion ray
// doesn't have to take the spatializer's spinlock once per source per frame.
//
// It is one frame behind when AudioSource::Compose() reads it — Compose runs
// first in the loop, updateAudio() after. Fine for a binary blocked/clear test,
// since the listener would have to cross a wall inside 16 ms for it to show, but
// don't build anything on this that needs to be exact.
static glm::vec3 listener{0.0f};

// A fixed pool of voices.
//
// miniaudio has its own fire-and-forget call, ma_engine_play_sound(), but it
// hands back nothing — so there is no way to set a per-sound volume with it, let
// alone move one after it starts. Owning the ma_sound objects ourselves is what
// buys both, and it is the same pool AudioSource hangs off.
//
// Fixed size, never grown: allocating in response to gameplay is how you get a
// frame hitch, and 32 simultaneous sounds is far more than a scene this size
// will ask for. Past that, new sounds are dropped rather than stealing a
// playing voice — a gunshot that never starts is less noticeable than one that
// cuts off halfway.
static const int MAX_VOICES = 32;

struct Voice
{
    ma_sound sound;
    bool active = false;

    // Bumped every time this slot is released. See SoundHandle in audio.h for
    // what that protects against.
    unsigned int generation = 0;
};

static Voice voices[MAX_VOICES];


// Tears down the sound in a slot and returns the slot to the pool.
//
// The only place `generation` is incremented — every path that frees a voice
// goes through here, so a handle can never outlive its voice without the bump
// that invalidates it.
static void releaseVoice(int slot)
{
    ma_sound_uninit(&voices[slot].sound);
    voices[slot].active = false;
    voices[slot].generation++;
}

// Frees every voice that has finished playing.
//
// This must stay on the main thread: ma_sound_uninit() tears down a node that
// the audio thread is walking, and miniaudio only guarantees that is safe from
// outside the callback.
static void reclaimVoices()
{
    for (int i = 0; i < MAX_VOICES; i++)
    {
        if (voices[i].active && ma_sound_at_end(&voices[i].sound))
            releaseVoice(i);
    }
}

// The one place a handle turns back into a voice. Null for a handle that was
// never issued, whose voice has already finished, or that was issued to an
// earlier occupant of the same slot.
static ma_sound* resolve(SoundHandle handle)
{
    if (!ready) return NULL;
    if (handle.slot < 0 || handle.slot >= MAX_VOICES) return NULL;

    Voice& voice = voices[handle.slot];
    if (!voice.active || voice.generation != handle.generation) return NULL;

    return &voice.sound;
}

// Claims a free slot and loads `path` into it, but does NOT start it.
//
// Split from startVoice() so the caller can configure the voice — position,
// attenuation, looping — while it is still silent. A 3D sound positioned after
// it starts gets one audio callback spatialized against the origin first, which
// is an audible click when the emitter is nowhere near world zero.
//
// Returns the slot, or -1 if audio is down, the pool is full, or the file could
// not be loaded.
static int initVoice(const char* path, ma_uint32 flags, const char* caller)
{
    if (!ready || path == NULL) return -1;

    reclaimVoices();

    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++)
    {
        if (!voices[i].active) { slot = i; break; }
    }

    if (slot < 0)
    {
        fprintf(stderr, "%s: all %d voices busy, dropped '%s'\n",
                caller, MAX_VOICES, path);
        return -1;
    }

    ma_result result = ma_sound_init_from_file(&engine, path, flags,
                                               NULL, NULL, &voices[slot].sound);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "%s: could not load '%s': %s\n",
                caller, path, ma_result_description(result));
        return -1;
    }

    return slot;
}

// Starts a voice loaded by initVoice() and hands back the handle to it. A dead
// handle back means the slot was released and nothing is playing.
static SoundHandle startVoice(int slot, const char* path, const char* caller)
{
    ma_result result = ma_sound_start(&voices[slot].sound);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "%s: could not start '%s': %s\n",
                caller, path, ma_result_description(result));

        // Not releaseVoice(): the slot was never marked active, so there is no
        // outstanding handle to invalidate and no generation to burn.
        ma_sound_uninit(&voices[slot].sound);
        return SoundHandle{};
    }

    voices[slot].active = true;
    return SoundHandle{slot, voices[slot].generation};
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
        if (voices[i].active) releaseVoice(i);
    }

    ma_engine_uninit(&engine);
    ready = false;

    // Every handle still held by a scene node is now stale, and resolve() bails
    // on !ready anyway — so an AudioSource destroyed after this point (main's
    // locals, on the way out) stops cleanly instead of double-uniniting.
}

bool audioReady()
{
    return ready;
}


// --- handles --- //

bool soundPlaying(SoundHandle handle)
{
    ma_sound* sound = resolve(handle);

    // at_end is checked as well as is_playing: a clip that ran out is still
    // "playing" to miniaudio until it is stopped, so without this a one-shot
    // would look alive for the rest of the frame it finished in.
    return sound != NULL && ma_sound_is_playing(sound) && !ma_sound_at_end(sound);
}

void setSoundPosition(SoundHandle handle, const glm::vec3& pos)
{
    // Safe to call from the main thread while the audio thread is mixing:
    // miniaudio guards the spatializer's vectors with a spinlock held just long
    // enough to copy three floats. That is its intended cross-thread path — do
    // not add a lock of your own around it, since anything that can block the
    // audio callback is a dropout waiting to happen.
    ma_sound* sound = resolve(handle);
    if (sound != NULL) ma_sound_set_position(sound, pos.x, pos.y, pos.z);
}

void setSoundVolume(SoundHandle handle, float volume)
{
    ma_sound* sound = resolve(handle);
    if (sound != NULL) ma_sound_set_volume(sound, volume);
}

void setSoundAttenuation(SoundHandle handle, float minDistance, float maxDistance,
                         float rolloff)
{
    ma_sound* sound = resolve(handle);
    if (sound == NULL) return;

    ma_sound_set_min_distance(sound, minDistance);
    ma_sound_set_max_distance(sound, maxDistance);
    ma_sound_set_rolloff(sound, rolloff);
}

void stopSound(SoundHandle handle)
{
    if (resolve(handle) == NULL) return;
    releaseVoice(handle.slot);
}


// --- playback --- //

bool playSound2D(const char* path, float volume)
{
    // DECODE pulls the whole file into memory up front instead of decoding it
    // on the audio thread — right for short SFX, wrong for music (that wants
    // MA_SOUND_FLAG_STREAM).
    //
    // NO_SPATIALIZATION is what makes this 2D. Without it miniaudio would
    // position the sound at the origin and attenuate it against the listener,
    // so a UI click would get quieter as the player walked away from world zero.
    const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;

    int slot = initVoice(path, flags, "playSound2D");
    if (slot < 0) return false;

    ma_sound_set_volume(&voices[slot].sound, volume);

    return startVoice(slot, path, "playSound2D").slot >= 0;
}

SoundHandle playSound3D(const char* path, const glm::vec3& pos, float volume, bool loop)
{
    // Same DECODE reasoning as the 2D path. Spatialization is simply the absence
    // of NO_SPATIALIZATION — the panner and the distance attenuator were always
    // in the graph, that flag just routed around them.
    int slot = initVoice(path, MA_SOUND_FLAG_DECODE, "playSound3D");
    if (slot < 0) return SoundHandle{};

    ma_sound* sound = &voices[slot].sound;

    ma_sound_set_volume(sound, volume);
    ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);

    // Everything below is set while the voice is still silent — see initVoice.
    ma_sound_set_position(sound, pos.x, pos.y, pos.z);

    // Linear, not miniaudio's default of inverse:
    //
    //   gain = 1 - rolloff * (clamp(d, min, max) - min) / (max - min)
    //
    // which reaches exactly 0 at maxDistance when rolloff is 1, so maxDistance
    // means what it looks like it means. Inverse is 1/x and never reaches zero —
    // it only stops falling at maxDistance, leaving every source audible forever
    // at whatever gain it had got down to (about -32 dB on these defaults).
    //
    // The trade is that linear is a straight line in amplitude, so it stays loud
    // further out and then dies quickly near the edge, where inverse drops off
    // fast up close and trails away. Inverse is the more natural-sounding of the
    // two; this one is the more controllable.
    //
    // Note rolloff above 1 makes the gain hit zero BEFORE maxDistance, and below
    // 1 leaves it audible at the boundary — with linear, rolloff and maxDistance
    // are not independent knobs.
    //
    // Callers with their own falloff curve in mind — matching the lighting's
    // intensity / (1 + d^2 / radius), say — can switch this to
    // ma_attenuation_model_none and drive setSoundVolume() per frame instead,
    // keeping miniaudio's panning while owning the distance term.
    ma_sound_set_attenuation_model(sound, ma_attenuation_model_linear);

    // Doppler off. miniaudio defaults it on, but it derives pitch from a
    // velocity nothing hands it — and a velocity recovered from a per-frame
    // position delta jitters with the frame time, which is audible as a warble
    // on a held tone. Nothing in a scene this size moves fast enough to earn it.
    ma_sound_set_doppler_factor(sound, 0.0f);

    return startVoice(slot, path, "playSound3D");
}


// --- per-frame --- //

void updateAudio(const glm::vec3& pos, const glm::vec3& front, const glm::vec3& up)
{
    if (!ready) return;

    // Sweep first. Nothing else frees a finished voice now that sources hold
    // handles across frames — the pool used to be swept only when a new sound
    // was played, which was fine for pure one-shots and would leak every slot a
    // long-lived source ever finished with.
    reclaimVoices();

    listener = pos;   // cached for the occlusion ray in AudioSource::Compose()

    // Listener 0; miniaudio supports several, but one pair of ears is the whole
    // point of a first-person game.
    ma_engine_listener_set_position (&engine, 0, pos.x,   pos.y,   pos.z);
    ma_engine_listener_set_direction(&engine, 0, front.x, front.y, front.z);
    ma_engine_listener_set_world_up (&engine, 0, up.x,    up.y,    up.z);
}

void setMasterVolume(float volume)
{
    if (!ready) return;
    ma_engine_set_volume(&engine, volume);
}

int activeVoiceCount()
{
    if (!ready) return 0;

    // No sweep here any more — updateAudio() runs one every frame, so a count
    // taken anywhere in the frame is already current. Sweeping again would only
    // reclaim voices that ended since, and doing that off the frame's own tick
    // makes the number jump around depending on where it's read.
    int count = 0;
    for (int i = 0; i < MAX_VOICES; i++)
    {
        if (voices[i].active) count++;
    }
    return count;
}


// --- AudioSource --- //

bool AudioSource::Play()
{
    Stop();

    handle = playSound3D(src.c_str(), glm::vec3(world[3]), volume, loop);
    setSoundAttenuation(handle, minDistance, maxDistance, rolloff);

    return handle.slot >= 0;
}

void AudioSource::Stop()
{
    stopSound(handle);
    handle = SoundHandle{};
}

static bool soundBlocked(const glm::vec3& origin, const glm::vec3& destination,
                        const std::vector<Tri>& tris)
{
    glm::vec3 dir = destination - origin;
    float maxDist = glm::length(dir);
    dir = glm::normalize(dir);


    const float EPS = 1e-6f;

    for (const Tri& t : tris)
    {
        glm::vec3 e1 = t.b - t.a;
        glm::vec3 e2 = t.c - t.a;
        glm::vec3 p  = glm::cross(dir, e2);
        float det = glm::dot(e1, p);
        if (glm::abs(det) < EPS) continue;   // ray runs parallel to the triangle

        float invDet = 1.0f / det;
        glm::vec3 tv = origin - t.a;

        float u = glm::dot(tv, p) * invDet;
        if (u < 0.0f || u > 1.0f) continue;

        glm::vec3 q = glm::cross(tv, e1);
        float v = glm::dot(dir, q) * invDet;
        if (v < 0.0f || u + v > 1.0f) continue;

        float hit = glm::dot(e2, q) * invDet;
        if (hit > EPS && hit < maxDist) return true;
    }
    return false;
}

void AudioSource::Compose()
{
    // The whole point of the node. This node's `world` was refreshed by its
    // parent moments ago, so the emitter follows whatever it hangs off with no
    // per-source bookkeeping at all. Column 3 is the translation — no decompose
    // needed, same trick the dynamic light lookup uses.
    //
    // No guard: setSoundPosition() no-ops on a dead handle, so a source that is
    // silent, finished, or never started costs one failed slot check.

    glm::vec3 pos = glm::vec3(world[3]);
    setSoundPosition(handle, pos);

    // Everything below costs a ray against every static triangle in the scene,
    // so it is worth asking whether anyone can hear this first. A source that
    // isn't playing is the common case for one-shots that have finished.
    if (soundPlaying(handle))
    {
        if (glm::distance(pos, listener) < maxDistance)
        {  
            if (soundBlocked(pos, listener, occluders))
                setSoundVolume(handle, volume * 0.3f);
            else
                setSoundVolume(handle, volume);
        }
        
    }

    Object::Compose();
}

AudioSource::~AudioSource()
{
    Stop();
}
