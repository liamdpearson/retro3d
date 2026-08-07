#pragma once

#include "../graphics/graphics.h"

// The engine's audio module.
//
// Unlike graphics.h, this header deliberately does NOT expose its backend
// state — miniaudio's types stay inside audio.cpp. Two reasons:
//
//   1. miniaudio.h is ~11.5k lines of declarations even with its
//      implementation switched off, and every translation unit that included
//      this header would pay to parse them.
//   2. Keeping ma_* out of gameplay code is what keeps replacing ma_engine
//      with a hand-written ma_device mixer later a change to one file rather
//      than a rewrite. Nothing above this line should know which library is
//      underneath it.
//
// So the "globals extern in the .h" convention of graphics.h is broken here on
// purpose. graphics.h itself is pulled in because AudioSource is a scene node —
// that is the scene graph, not the audio backend, so it doesn't breach the rule.

// Brings up the audio device and mixing graph. Returns 0 on success, and the
// miniaudio result code otherwise.
//
// Failure is deliberately not fatal: a machine with no working audio device
// should still run the game in silence rather than refuse to launch, so the
// caller is free to ignore the result. Every other function in this header is
// a safe no-op until this succeeds.
int initAudio();

// Stops every voice and closes the device. Safe to call even if initAudio()
// failed or was never called.
void shutdownAudio();

bool audioReady();


// --- handles --- //

// An opaque reference to one playing voice.
//
// A bare slot index would not be safe to hold across frames: voices are a fixed
// pool and a slot is reused the moment the sound in it finishes, so a stale
// reference would silently start moving and muting whatever sound landed there
// next. `generation` is bumped every time a slot is released, so a handle from
// a previous occupant fails the match and every operation on it quietly becomes
// a no-op instead.
//
// A default-constructed handle is dead, which is what a source that has never
// played holds.
struct SoundHandle
{
    int slot = -1;
    unsigned int generation = 0;
};

// True while the handle still refers to the voice it was issued for AND that
// voice is still running. A voice that reached the end of its clip reports false
// here even in the frame before the sweep in updateAudio() reclaims it.
bool soundPlaying(SoundHandle handle);

// These are all no-ops on a dead or stale handle, so callers never have to check
// first — an AudioSource can push its position every frame whether or not the
// sound it started is still playing.
void setSoundPosition(SoundHandle handle, const glm::vec3& pos);
void setSoundVolume(SoundHandle handle, float volume);
void setSoundAttenuation(SoundHandle handle, float minDistance, float maxDistance,
                         float rolloff);

// Stops the voice and returns its slot to the pool immediately, invalidating
// every handle to it.
//
// Note this is deliberately not just miniaudio's stop: a merely-stopped voice is
// not "at end", so the per-frame sweep would never notice it and the slot would
// stay claimed for the life of the process.
void stopSound(SoundHandle handle);


// --- playback --- //

// Fire-and-forget, non-spatial playback: UI clicks, music stings, and anything
// else that shouldn't move with the world — including a first-person weapon,
// which is always at the listener's head anyway.
//
// `volume` is linear, 1.0f being unattenuated. Returns false if audio is down,
// the file could not be loaded, or every voice is already busy.
bool playSound2D(const char* path, float volume = 1.0f);

// Fire-and-forget playback from a point in the world: panned by the angle to the
// listener and attenuated by distance to it.
//
// The returned handle is only worth keeping if the emitter can move. A sound at
// a fixed point already stays correct as the player walks around it, because
// that is the listener moving, not the source — updateAudio() handles it.
SoundHandle playSound3D(const char* path, const glm::vec3& pos,
                        float volume = 1.0f, bool loop = false);


// --- per-frame --- //

// The audio tick: moves the listener onto the camera, and frees any voice that
// has finished playing.
//
// Call once per frame, AFTER the Compose pass. camera.pos/front/up are only
// valid once Compose has run, and a listener built from last frame's camera lags
// the picture in exactly the way the Compose/Draw split exists to avoid.
//
// The vectors go straight through with no axis flipping: miniaudio's listener is
// right-handed with forward at -Z and up at +Y, which is the same convention
// Camera::Compose() reads out of the world matrix.
//
// Pass the camera's own `up` rather than a hardcoded world up. It comes out of
// the composed basis already perpendicular to `front`, so it can't go degenerate
// when looking straight up, and passing it is what makes the stereo field roll
// when the camera rolls — which is the whole reason transforms are kept as
// matrices in the first place.
void updateAudio(const glm::vec3& pos, const glm::vec3& front, const glm::vec3& up);

// Master volume over everything, linear, 1.0f unattenuated.
void setMasterVolume(float volume);

// How many voices are currently playing. Cheap; useful on a debug label while
// tuning how large the pool actually needs to be.
int activeVoiceCount();


// --- AudioSource --- //

// A sound that is a node in the scene graph.
//
// This is what makes "keep the sound accurate while things move" cost no
// bookkeeping at all. Compose() already walks the whole graph every frame, so a
// source parented to a moving mesh inherits that motion the same way a child
// mesh does; the listener end is handled by updateAudio(). Neither side needs to
// know the other exists, and a source parented to a pivot, a mover, or the
// player works without a special case.
struct AudioSource : Object
{
    std::string src;

    float volume = 1.0f;

    // Inside minDistance the sound plays at full volume, then falls off in a
    // straight line to silence at maxDistance — the attenuation model is linear
    // (see playSound3D), so maxDistance really is the point where the source
    // goes quiet rather than just the point where it stops getting quieter.
    //
    // Metres, like everything else in the engine — which is also the scale
    // miniaudio's defaults assume, so these numbers mean what they look like.
    //
    // `rolloff` steepens that line: 1 lands on silence exactly at maxDistance,
    // above 1 hits silence early and leaves a dead ring inside the radius, below
    // 1 never quite gets there. Leave it at 1 unless you want one of those.
    float minDistance = 1.0f;
    float maxDistance = 40.0f;
    float rolloff = 1.0f;

    bool loop = false;

    SoundHandle handle;

    // Kept alive explicitly: declaring the constructor below would otherwise
    // suppress the implicit one, and a source built field-by-field is still the
    // convenient way to author one in code.
    AudioSource() = default;

    // Field-order constructor, for the scene importer. buildNode() has all seven
    // values in hand from the JSON and nothing to hang them off yet, so it wants
    // to build the node in a single expression. Argument order matches the
    // declaration order above; `handle` is deliberately not settable, since a
    // node that has never played has nothing to hold a handle to.
    AudioSource(const Transform& transform, const std::string& src, float volume,
                float minDistance, float maxDistance, float rolloff, bool loop)
        : Object(transform), src(src), volume(volume),
          minDistance(minDistance), maxDistance(maxDistance), rolloff(rolloff),
          loop(loop) {}

    // Starts `src` at this node's current world position. Returns false if audio
    // is down, the file failed to load, or the pool is full.
    //
    // Reads `world`, which Compose() is what fills in — call this before the
    // first Compose and the sound starts one frame stale (at the origin for a
    // node that has never been composed at all). Seed `world` by hand first if
    // that matters, as game.cpp does for the test rig.
    bool Play();

    // Restarts rather than layering: a source is one emitter, so a second voice
    // from the same node would only phase against the first.
    void Stop();

    // Pushes this node's composed world position at the voice, then recurses.
    void Compose() override;

    // A deleted source must not leave its voice playing forever at wherever the
    // node happened to be.
    ~AudioSource() override;
};
