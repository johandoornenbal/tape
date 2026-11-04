#pragma once
#include <array>
namespace controls {
constexpr std::array<const char*,9> ids{"input","tilt","shape","flutter","flutterSpeed","bias","headBump","headFrequency","output"};
constexpr std::array<const char*,10> names7{"Encode amount","Encode frequency","Tape drive","Flutter","Flutter speed","Bias","Head bump","Head frequency","Decode amount","Decode frequency"};
constexpr std::array<const char*,9> names{"Input","Tilt","Shape","Flutter","Flutter speed","Bias","Head bump","Head frequency","Output"};
constexpr std::array<const char*,10> tips7{
    "Original Dubly encode amount.","Original normalized encode filter frequency.",
    "Drive into the tape circuit. 50% is unity gain.","Tape flutter depth. Zero removes modulation.",
    "Tape flutter modulation speed.","Tape bias. 50% is neutral.","Low-frequency head bump amount.",
    "Head bump resonance frequency, 25 to 200 Hz.","Original Dubly decode amount.","Original normalized decode filter frequency."};
constexpr std::array<const char*,9> tips{
    "Drive into the tape circuit. 50% is unity gain.","Balance the original Dubly encode and decode stages.",
    "Set the Dubly encode/decode filter shape.","Tape flutter depth. Zero removes modulation.",
    "Tape flutter modulation speed.","Tape bias. 50% is neutral.","Low-frequency head bump amount.",
    "Head bump resonance frequency, 25 to 200 Hz.","Output gain before the original clipping stage. 50% is unity gain."};
inline int count(int version) { return version==7?10:9; }
inline juce::String id(int version,int index) {
    if(version==9) return ids[static_cast<size_t>(index)];
    return "v"+juce::String(version)+"_"+juce::String(index);
}
inline const char* name(int version,int index) { return version==7?names7[static_cast<size_t>(index)]:names[static_cast<size_t>(index)]; }
inline const char* tip(int version,int index) { return version==7?tips7[static_cast<size_t>(index)]:tips[static_cast<size_t>(index)]; }
inline juce::String display(int i,double v) {
    if(i==7) return juce::String(v*v*175.0+25.0,1)+" Hz";
    return juce::String(v*100.0,1)+" %";
}
inline double parse(int i,const juce::String& text) {
    const auto v=text.getDoubleValue();
    return i==7?std::sqrt(juce::jlimit(0.0,1.0,(v-25.0)/175.0)):v/100.0;
}
}
