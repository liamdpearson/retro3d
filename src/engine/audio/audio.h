#pragma once

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
// purpose.

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

// Fire-and-forget, non-spatial playback: UI clicks, music stings, and anything
// else that shouldn't move with the world. Phase 3's AudioSource is what you
// want for anything with a position.
//
// `volume` is linear, 1.0f being unattenuated. Returns false if audio is down,
// the file could not be loaded, or every voice is already busy.
bool playSound2D(const char* path, float volume = 1.0f);

// Master volume over everything, linear, 1.0f unattenuated.
void setMasterVolume(float volume);

// How many one-shot voices are currently playing. Cheap; useful on a debug
// label while tuning how large the pool actually needs to be.
int activeVoiceCount();
