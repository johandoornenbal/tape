#pragma once
#include <algorithm>

// The existing Airwindows gains are (2 * input)^2 and 2 * output.
// Preserve their product without introducing any additional audio processing.
class InputOutputLink {
public:
    void reset(float input,float output) {
        referenceOutput=output;
        product=static_cast<double>(input)*input*output;
        valid=input>0;
    }
    float outputFor(float input) {
        // No finite inverse exists at mute. Establish the reference on the first
        // positive input if linking was enabled while Input was zero.
        if(!valid) { if(input>0) reset(input,referenceOutput); return referenceOutput; }
        if(input<=0) return product>0?1.f:0.f;
        return static_cast<float>(std::clamp(product/(static_cast<double>(input)*input),0.0,1.0));
    }
private:
    double product=0;
    float referenceOutput=.5f;
    bool valid=false;
};
