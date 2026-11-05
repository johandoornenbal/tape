#pragma once
#include "LoudnessMatch.h"
#include <atomic>

// One independent learned adjustment for each tape model. Audio analysis and
// post-engine gain are wrapper features; the Airwindows engines are untouched.
struct TapeMatchState {
    std::atomic<float> db{0},inputLufs{0},outputLufs{0};
    std::atomic<bool> learned{false};
    void clear() { learned=false; db=0; inputLufs=0; outputLufs=0; }
};
