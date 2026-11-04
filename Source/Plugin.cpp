#include <JuceHeader.h>
#include "TapeEngine.h"
#include <atomic>

namespace controls {
constexpr std::array<const char*,9> ids{"input","tilt","shape","flutter","flutterSpeed","bias","headBump","headFrequency","output"};
constexpr std::array<const char*,9> names{"Input","Tilt","Shape","Flutter","Flutter speed","Bias","Head bump","Head frequency","Output"};
constexpr std::array<const char*,9> tips{
    "Drive into the tape circuit. 50% is unity gain.",
    "Balance the upstream Dubly encode and decode stages.",
    "Set the Dubly encode/decode filter shape.",
    "Tape flutter depth. Zero removes the modulation.",
    "Tape flutter modulation speed.",
    "Tape bias. 50% is the neutral bias setting.",
    "Low-frequency head bump amount.",
    "Head bump resonance frequency, 25 to 200 Hz.",
    "Output gain before the original ClipOnly3 stage. 50% is unity gain."};
juce::String display(int i, double v) {
    if(i==7) return juce::String(v*v*175.0+25.0,1)+" Hz";
    return juce::String(v*100.0,1)+" %";
}
double parse(int i,const juce::String& text) {
    const auto v=text.getDoubleValue();
    return i==7?std::sqrt(juce::jlimit(0.0,1.0,(v-25.0)/175.0)):v/100.0;
}
}
class TapeProcessor final : public juce::AudioProcessor {
public:
    TapeProcessor() : AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
        state(*this,nullptr,"ToTape9State",layout()) {
        for(size_t i=0;i<values.size();++i) values[i]=state.getRawParameterValue(controls::ids[i]);
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        for(int i=0;i<9;++i) {
            auto attributes=juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction([i](float v,int) { return controls::display(i,v); })
                .withValueFromStringFunction([i](const juce::String& s) { return static_cast<float>(controls::parse(i,s)); });
            p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{controls::ids[static_cast<size_t>(i)],1},
                controls::names[static_cast<size_t>(i)],juce::NormalisableRange<float>(0,1),.5f,attributes));
        }
        return p;
    }
    void prepareToPlay(double rate,int) override { engine.reset(rate); setLatencySamples(1); peaks[0]=0; peaks[1]=0; }
    void releaseResources() override { peaks[0]=0; peaks[1]=0; }
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    template<class T> void process(juce::AudioBuffer<T>& b) {
        juce::ScopedNoDenormals guard;
        if(b.getNumChannels()<2) { b.clear(); return; }
        for(int i=0;i<9;++i) engine.setParameter(i,values[static_cast<size_t>(i)]->load());
        T* data[]{b.getWritePointer(0),b.getWritePointer(1)};
        if constexpr (std::is_same_v<T,float>) engine.processReplacing(data,data,b.getNumSamples());
        else engine.processDoubleReplacing(data,data,b.getNumSamples());
        for(int c=0;c<2;++c) {
            const auto peak=static_cast<float>(b.getMagnitude(c,0,b.getNumSamples()));
            auto old=peaks[c].load(std::memory_order_relaxed);
            while(old<peak && !peaks[c].compare_exchange_weak(old,peak,std::memory_order_relaxed)) {}
        }
    }
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { process(b); }
    void processBlock(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { process(b); }
    bool supportsDoublePrecisionProcessing() const override { return true; }
    const juce::String getName() const override { return "ToTape Studio"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& dst) override {
        if(auto xml=state.copyState().createXml()) copyXmlToBinary(*xml,dst);
    }
    void setStateInformation(const void* data,int size) override {
        if(auto xml=getXmlFromBinary(data,size); xml && xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
    }
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> peaks[2]{};
private:
    TapeEngine engine;
    std::array<std::atomic<float>*,9> values{};
};

namespace style {
const juce::Colour cream{0xfff4eddb}, ink{0xff252b29}, gold{0xffd7ac61}, muted{0xff858b80};
class Look final : public juce::LookAndFeel_V4 {
public:
    Look() {
        setColour(juce::Slider::textBoxTextColourId, cream);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::highlightColourId, gold.withAlpha(.4f));
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider&) override {
        auto r=juce::Rectangle<float>(static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)).reduced(16);
        auto radius=juce::jmin(r.getWidth(),r.getHeight())*.5f;
        auto cx=r.getCentreX(), cy=r.getCentreY();
        juce::Path track, active;
        track.addCentredArc(cx,cy,radius,radius,0,start,end,true);
        active.addCentredArc(cx,cy,radius,radius,0,start,start+pos*(end-start),true);
        g.setColour(cream.withAlpha(.12f)); g.strokePath(track,juce::PathStrokeType(3));
        g.setColour(gold); g.strokePath(active,juce::PathStrokeType(3));
        auto dial=juce::Rectangle<float>(radius*1.58f,radius*1.58f).withCentre({cx,cy});
        g.setColour(juce::Colours::black.withAlpha(.3f)); g.fillEllipse(dial.translated(0,5));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff535a52),cx,cy-radius,ink,cx,cy+radius,false));
        g.fillEllipse(dial); g.setColour(cream.withAlpha(.15f)); g.drawEllipse(dial,1);
        const auto angle=start+pos*(end-start);
        g.setColour(gold);
        g.drawLine(cx+std::sin(angle)*radius*.48f,cy-std::cos(angle)*radius*.48f,
                   cx+std::sin(angle)*radius*.68f,cy-std::cos(angle)*radius*.68f,3);
    }
};
}

class TapeEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TapeEditor(TapeProcessor& p) : AudioProcessorEditor(p), processor(p) {
        setLookAndFeel(&look);
        for(size_t i=0;i<knobs.size();++i) {
            auto& s=knobs[i]; addAndMakeVisible(s);
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,115,23);
            s.setName(controls::names[i]);
            s.setTooltip(juce::String(controls::tips[i])+" Double-click to reset.");
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,controls::ids[i],s);
            s.textFromValueFunction=[i](double v) { return controls::display(static_cast<int>(i),v); };
            s.valueFromTextFunction=[i](const juce::String& text) { return controls::parse(static_cast<int>(i),text); };
            s.updateText(); s.setDoubleClickReturnValue(true,.5);
        }
        reset.setButtonText("RESET TO DEFAULT"); reset.setTooltip("Reset all nine parameters to the Airwindows defaults.");
        reset.onClick=[this] {
            for(auto* parameter:processor.getParameters()) { parameter->beginChangeGesture(); parameter->setValueNotifyingHost(.5f); parameter->endChangeGesture(); }
        };
        addAndMakeVisible(reset);
        reset.setColour(juce::TextButton::buttonColourId,style::ink);
        reset.setColour(juce::TextButton::textColourOffId,style::gold);
        for(int c=0;c<2;++c) levels[c]=processor.peaks[c].load(std::memory_order_relaxed);
        setSize(980,760); startTimerHz(30);
    }
    ~TapeEditor() override { stopTimer(); setLookAndFeel(nullptr); }
    void resized() override {
        const int top[]{0,1,2,5,8},bottom[]{3,4,6,7};
        for(int j=0;j<5;++j) knobs[static_cast<size_t>(top[j])].setBounds(48+j*180,370,164,150);
        for(int j=0;j<4;++j) knobs[static_cast<size_t>(bottom[j])].setBounds(138+j*180,552,164,150);
        reset.setBounds(750,42,180,30);
    }
    void paint(juce::Graphics& g) override {
        using namespace style;
        g.fillAll(cream);
        g.setColour(ink); g.setFont(juce::FontOptions(11.f));
        g.drawText("YODO AUDIO    /    AIRWINDOWS DSP",32,17,650,20,juce::Justification::left);
        g.setFont(juce::FontOptions(40.f)); g.drawText("ToTape",30,40,240,55,juce::Justification::left);
        g.setFont(juce::FontOptions(13.f)); g.drawText("S T U D I O   /   0 9",248,61,300,20,juce::Justification::left);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff343d36),0,108,juce::Colour(0xff1b2420),0,720,false));
        g.fillRoundedRectangle(16,108,948,610,16);
        g.setColour(gold.withAlpha(.2f)); g.drawRoundedRectangle({17.f,109.f,946.f,608.f},15,1);
        for(float x:{30.f,950.f}) for(float y:{122.f,704.f}) {
            g.setColour(juce::Colour(0xff111a15)); g.fillEllipse(x-4,y-4,8,8);
            g.setColour(juce::Colour(0xff646c5b)); g.drawLine(x-2,y+1,x+2,y-1,1);
        }
        // Recessed transport window, with two vector-drawn tape reels.
        g.setColour(juce::Colour(0xff101713)); g.fillRoundedRectangle(48,139,568,182,10);
        g.setColour(cream.withAlpha(.08f)); g.drawRoundedRectangle({49.f,140.f,566.f,180.f},9,1);
        g.setColour(gold.withAlpha(.4f)); g.drawLine(196,279,468,279,3);
        drawReel(g,190,226,angle); drawReel(g,474,226,angle);
        g.setColour(cream.withAlpha(.7f)); g.setFont(juce::FontOptions(10.f));
        g.drawText("STEREO TAPE PROCESSOR",265,173,140,30,juce::Justification::centred);
        g.setColour(gold); g.setFont(juce::FontOptions(32.f));
        g.drawText("9",285,208,100,44,juce::Justification::centred);
        g.setColour(muted); g.setFont(juce::FontOptions(10.f));
        g.drawText("CHRIS JOHNSON",265,263,140,20,juce::Justification::centred);
        g.setColour(gold); g.setFont(juce::FontOptions(11.f));
        g.drawText("OUTPUT LEVEL",650,149,270,22,juce::Justification::left);
        drawMeter(g,0,650,190); drawMeter(g,1,650,233);
        g.setColour(muted); g.setFont(juce::FontOptions(10.f));
        g.drawText("PEAK  /  dBFS",650,285,270,18,juce::Justification::left);
        g.drawText("-60                      -24           -12             0",650,270,270,16,juce::Justification::left);
        g.setColour(cream.withAlpha(.12f)); g.drawHorizontalLine(341,49,931);
        g.drawHorizontalLine(533,138,842);
        const int top[]{0,1,2,5,8},bottom[]{3,4,6,7};
        g.setColour(gold); g.setFont(juce::FontOptions(11.f));
        for(int j=0;j<5;++j) g.drawText(juce::String(controls::names[static_cast<size_t>(top[j])]).toUpperCase(),48+j*180,351,164,22,juce::Justification::centred);
        for(int j=0;j<4;++j) g.drawText(juce::String(controls::names[static_cast<size_t>(bottom[j])]).toUpperCase(),138+j*180,536,164,22,juce::Justification::centred);
        g.setColour(ink.withAlpha(.65f)); g.setFont(juce::FontOptions(11.f));
        g.drawText("ToTape9 by Airwindows  /  Independent studio edition",32,726,710,24,juce::Justification::left);
        g.drawText("STEREO  /  FLOAT / DOUBLE",700,726,248,24,juce::Justification::right);
    }
private:
    void drawReel(juce::Graphics& g,float x,float y,float rotation) {
        using namespace style;
        g.setColour(juce::Colours::black.withAlpha(.5f)); g.fillEllipse(x-72,y-67,144,144);
        g.setColour(juce::Colour(0xff564331)); g.fillEllipse(x-66,y-66,132,132);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb5b6a7),x-60,y-60,juce::Colour(0xff626c62),x+60,y+60,false));
        g.fillEllipse(x-59,y-59,118,118);
        g.setColour(cream.withAlpha(.4f)); g.drawEllipse(x-59,y-59,118,118,1);
        for(int i=0;i<3;++i) {
            auto a=rotation+static_cast<float>(i)*juce::MathConstants<float>::twoPi/3;
            juce::Path hole; hole.addRoundedRectangle(-12,-48,24,32,9);
            hole.applyTransform(juce::AffineTransform::rotation(a).translated(x,y));
            g.setColour(juce::Colour(0xff302b23)); g.fillPath(hole);
        }
        g.setColour(ink); g.fillEllipse(x-20,y-20,40,40);
        g.setColour(gold); g.drawEllipse(x-13,y-13,26,26,2);
        g.setColour(cream); g.fillEllipse(x-4,y-4,8,8);
    }
    void drawMeter(juce::Graphics& g,int c,int x,int y) {
        const auto db=juce::Decibels::gainToDecibels(levels[c],-60.f);
        g.setColour(style::cream.withAlpha(.8f)); g.setFont(juce::FontOptions(10.f));
        g.drawText(c==0?"L":"R",x,y,15,20,juce::Justification::left);
        for(int i=0;i<30;++i) {
            const bool active=db>-60.f+static_cast<float>(i)*2.f;
            auto colour=i>26?juce::Colour(0xffd37755):style::gold;
            g.setColour(colour.withAlpha(active?1.f:.12f));
            g.fillRoundedRectangle(static_cast<float>(x+20+i*6),static_cast<float>(y),4,17,1);
        }
        g.setColour(style::cream); g.drawText(db<=-60?"-inf":juce::String(db,1),x+208,y,60,20,juce::Justification::right);
    }
    void timerCallback() override {
        for(int c=0;c<2;++c) levels[c]=juce::jmax(processor.peaks[c].exchange(0,std::memory_order_relaxed),levels[c]*.86f);
        if(juce::jmax(levels[0],levels[1])>.0001f) angle+=.025f;
        repaint(48,139,884,183);
    }
    TapeProcessor& processor;
    style::Look look;
    std::array<juce::Slider,9> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,9> attachments;
    juce::TextButton reset;
    juce::TooltipWindow tooltip{this,600};
    float levels[2]{},angle=0;
};
juce::AudioProcessorEditor* TapeProcessor::createEditor() { return new TapeEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TapeProcessor; }
