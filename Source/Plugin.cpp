#include <JuceHeader.h>
#include "TapeEngine.h"
#include <atomic>

#include "TapeControls.h"
#include "TapeEngine7.h"
#include "TapeEngine8.h"
#include "OutputMeter.h"
#include "MeterBallistics.h"
#include "GainLinkAttachment.h"
#include "TapeLevelMatch.h"

class TapeProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    TapeProcessor() : AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
        state(*this,nullptr,"ToTape9State",layout()) {
        for(int version=7;version<=9;++version)
            for(int i=0;i<controls::count(version);++i) values[static_cast<size_t>(version-7)][static_cast<size_t>(i)]=state.getRawParameterValue(controls::id(version,i));
        versionValue=state.getRawParameterValue("version");
        defaults=state.copyState();
        for(int version=7;version<=9;++version) {
            matchEnabled[static_cast<size_t>(version-7)]=state.getRawParameterValue(controls::matchId(version));
            original[static_cast<size_t>(version-7)]=state.getRawParameterValue(controls::originalId(version));
        }
        startTimerHz(10);
    }
    ~TapeProcessor() override { stopTimer(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        // Keep the original nine parameter IDs and indices for existing sessions/automation.
        for(int version:{9,8,7}) for(int i=0;i<controls::count(version);++i) {
            auto attributes=juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction([i](float v,int) { return controls::display(i,v); })
                .withValueFromStringFunction([i](const juce::String& text) { return static_cast<float>(controls::parse(i,text)); });
            const auto name=(version==9?juce::String():"ToTape"+juce::String(version)+" ")+controls::name(version,i);
            p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{controls::id(version,i),1},
                name,juce::NormalisableRange<float>(0,1),.5f,attributes));
        }
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"version",1},"Tape version",
            juce::StringArray{"ToTape7","ToTape8","ToTape9"},2));
        for(int version:{8,9})
            p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{controls::linkId(version),1},
                "ToTape"+juce::String(version)+" Link Input / Output",false));
        for(int version:{7,8,9}) {
            p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{controls::matchId(version),1},
                "ToTape"+juce::String(version)+" Level Match",true));
            p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{controls::originalId(version),1},
                "ToTape"+juce::String(version)+" Original A/B",false));
        }
        return p;
    }
    int selectedVersion() const { return juce::jlimit(7,9,7+juce::roundToInt(versionValue->load())); }
    void prepareToPlay(double rate,int blockSize) override {
        sampleRate=rate; engine.reset(rate); engine7.reset(rate); engine8.reset(rate);
        activeVersion=selectedVersion(); setLatencySamples(1); peaks[0]=0; peaks[1]=0;
        meterTimeoutSeconds.store(juce::jmax(.15,2.0*blockSize/rate),std::memory_order_relaxed);
        meterSequence.fetch_add(1,std::memory_order_release);
        loudness.prepare(rate); learning=false; progress=0; command=0;
        lastDryL=lastDryR=0;
        gainSmooth.reset(rate,.02); blendSmooth.reset(rate,.005);
        gainSmooth.setCurrentAndTargetValue(targetGain(activeVersion));
        blendSmooth.setCurrentAndTargetValue(original[static_cast<size_t>(activeVersion-7)]->load()>.5f?0.:1.);
    }
    template<class Engine,class T> void render(Engine& e,juce::AudioBuffer<T>& b,int version) {
        for(int i=0;i<controls::count(version);++i) e.setParameter(i,values[static_cast<size_t>(version-7)][static_cast<size_t>(i)]->load());
        // Fixed stack storage preserves dry input without allocating on the audio thread.
        // Measure the original input and untrimmed tape output, then apply a separate gain.
        std::array<T,256> dryL{},dryR{};
        for(int offset=0;offset<b.getNumSamples();offset+=256) {
            const int count=juce::jmin(256,b.getNumSamples()-offset);
            auto* left=b.getWritePointer(0)+offset; auto* right=b.getWritePointer(1)+offset;
            std::copy_n(left,count,dryL.data()); std::copy_n(right,count,dryR.data());
            T* data[]{left,right};
            if constexpr (std::is_same_v<T,float>) e.processReplacing(data,data,count);
            else e.processDoubleReplacing(data,data,count);
            gainSmooth.setTargetValue(targetGain(version));
            blendSmooth.setTargetValue(original[static_cast<size_t>(version-7)]->load()>.5f?0.:1.);
            for(int i=0;i<count;++i) {
                const auto dryIndex=static_cast<size_t>(i);
                if(loudness.learning()) {
                    loudness.sample(dryL[dryIndex],dryR[dryIndex],left[i],right[i]);
                    if(loudness.complete()) { finishLearn(); gainSmooth.setTargetValue(targetGain(version)); }
                }
                // All tape engines report one sample of fixed latency. Align Original
                // to that same latency for A/B; it never passes through the tape engine.
                const auto alignedL=lastDryL,alignedR=lastDryR;
                lastDryL=dryL[dryIndex]; lastDryR=dryR[dryIndex];
                const auto gain=gainSmooth.getNextValue(),blend=blendSmooth.getNextValue();
                if(blend<=0) { left[i]=static_cast<T>(alignedL); right[i]=static_cast<T>(alignedR); }
                else if(blend>=1) {
                    if(gain<1 || gain>1) { left[i]=static_cast<T>(left[i]*gain); right[i]=static_cast<T>(right[i]*gain); }
                } else {
                    left[i]=static_cast<T>(alignedL*(1-blend)+left[i]*gain*blend);
                    right[i]=static_cast<T>(alignedR*(1-blend)+right[i]*gain*blend);
                }
            }
        }
        progress=loudness.seconds();
    }
    void releaseResources() override { peaks[0]=0; peaks[1]=0; meterSequence.fetch_add(1,std::memory_order_release); }
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    template<class T> void process(juce::AudioBuffer<T>& b) {
        juce::ScopedNoDenormals guard;
        if(b.getNumChannels()<2) { b.clear(); return; }
        const int version=selectedVersion();
        if(version!=activeVersion) {
            loudness.cancel(); learning=false; progress=0; learnResult=0;
            // Reset only the entering engine, on the audio thread, without allocation.
            if(version==7) engine7.reset(sampleRate);
            else if(version==8) engine8.reset(sampleRate);
            else engine.reset(sampleRate);
            activeVersion=version;
        }
        handleCommand(version);
        if(version==7) render(engine7,b,version);
        else if(version==8) render(engine8,b,version);
        else render(engine,b,version);
        publishMeters(b);
    }
    template<class T> void publishMeters(juce::AudioBuffer<T>& b) {
        for(int c=0;c<2;++c) {
            const auto peak=static_cast<float>(b.getMagnitude(c,0,b.getNumSamples()));
            auto old=peaks[c].load(std::memory_order_relaxed);
            while(old<peak && !peaks[c].compare_exchange_weak(old,peak,std::memory_order_relaxed)) {}
        }
        meterSequence.fetch_add(1,std::memory_order_release);
    }
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { process(b); }
    void processBlock(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { process(b); }
    template<class T> void bypass(juce::AudioBuffer<T>& b) {
        handleCommand(selectedVersion());
        if(b.getNumChannels()>=2 && b.getNumSamples()>0) {
            lastDryL=b.getSample(0,b.getNumSamples()-1); lastDryR=b.getSample(1,b.getNumSamples()-1);
            publishMeters(b);
        }
    }
    void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { bypass(b); }
    void processBlockBypassed(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { bypass(b); }
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
        auto saved=state.copyState();
        saved.setProperty("uiSize",uiSize.load(),nullptr);
        for(int version=7;version<=9;++version) {
            const auto prefix="v"+juce::String(version)+"_";
            const auto& match=matches[static_cast<size_t>(version-7)];
            saved.setProperty(prefix+"matchDb",match.db.load(),nullptr);
            saved.setProperty(prefix+"hasMatch",match.learned.load(),nullptr);
            saved.setProperty(prefix+"inputLufs",match.inputLufs.load(),nullptr);
            saved.setProperty(prefix+"outputLufs",match.outputLufs.load(),nullptr);
        }
        if(auto xml=saved.createXml()) copyXmlToBinary(*xml,dst);
    }
    void setStateInformation(const void* data,int size) override {
        if(auto xml=getXmlFromBinary(data,size); xml && xml->hasTagName(state.state.getType()))
        {
            auto restored=juce::ValueTree::fromXml(*xml);
            uiSize=juce::jlimit(1,3,static_cast<int>(restored.getProperty("uiSize",2)));
            // Legacy ToTape9 sessions have no version or extra banks. Supply defaults
            // even when loading into an instance that previously selected another version.
            for(const auto& child:defaults) {
                const auto id=child.getProperty("id");
                if(!restored.getChildWithProperty("id",id).isValid()) restored.addChild(child.createCopy(),-1,nullptr);
            }
            for(int version=7;version<=9;++version) {
                const auto prefix="v"+juce::String(version)+"_";
                auto& match=matches[static_cast<size_t>(version-7)];
                const auto db=static_cast<float>(restored.getProperty(prefix+"matchDb",0.f));
                match.db=std::isfinite(db)?juce::jlimit(-24.f,24.f,db):0.f;
                match.learned=std::isfinite(db)&&static_cast<bool>(restored.getProperty(prefix+"hasMatch",false));
                const auto in=static_cast<float>(restored.getProperty(prefix+"inputLufs",0.f));
                const auto out=static_cast<float>(restored.getProperty(prefix+"outputLufs",0.f));
                match.inputLufs=std::isfinite(in)?in:0.f; match.outputLufs=std::isfinite(out)?out:0.f;
            }
            command=4; learning=false; progress=0; learnResult=0;
            state.replaceState(restored);
        }
    }
    double targetGain(int version) const {
        const auto index=static_cast<size_t>(version-7);
        return matches[index].learned.load() && matchEnabled[index]->load()>.5f
            ? std::pow(10.,static_cast<double>(matches[index].db.load())/20.) : 1.;
    }
    void requestLearn() {
        const auto version=selectedVersion();
        command=version*8+(learning.load() && learningVersion.load()==version?2:1);
    }
    void resetMatch() {
        const auto version=selectedVersion();
        matches[static_cast<size_t>(version-7)].clear();
        learning=false; progress=0; learnResult=0; command=version*8+3; matchRevision.fetch_add(1);
    }
    std::array<TapeMatchState,3> matches;
    std::atomic<bool> learning{false};
    std::atomic<int> learningVersion{9},learnResult{0};
    std::atomic<float> progress{0};
    std::atomic<int> command{0};
    std::atomic<uint32_t> matchRevision{0};
    std::atomic<int> uiSize{2};
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> peaks[2]{};
    std::atomic<uint32_t> meterSequence{0};
    std::atomic<double> meterTimeoutSeconds{.15};
private:
    void finishLearn() {
        const auto result=loudness.finish();
        if(result==LoudnessMatch::Result::success || result==LoudnessMatch::Result::limited) {
            auto& match=matches[static_cast<size_t>(learningVersion.load()-7)];
            match.db=static_cast<float>(loudness.trimDb);
            match.inputLufs=static_cast<float>(loudness.inputLufs); match.outputLufs=static_cast<float>(loudness.outputLufs);
            match.learned=true; matchRevision.fetch_add(1);
        }
        learnResult=static_cast<int>(result); learning=false;
    }
    void handleCommand(int version) {
        const auto request=command.exchange(0);
        if(request==4) { loudness.cancel(); learning=false; return; }
        if(request==0) return;
        const auto model=request/8,action=request%8;
        if(action==3) {
            matches[static_cast<size_t>(model-7)].clear();
            if(learningVersion.load()==model) { loudness.cancel(); learning=false; progress=0; learnResult=0; }
        } else if(model==version) {
            if(action==1) { loudness.start(); learningVersion=version; learning=true; progress=0; learnResult=0; }
            if(action==2 && learning.load() && learningVersion.load()==version) finishLearn();
        }
    }
    void timerCallback() override {
        const auto revision=matchRevision.load();
        if(revision!=notifiedRevision) {
            notifiedRevision=revision;
            updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
        }
    }
    LoudnessMatch loudness;
    juce::SmoothedValue<double> gainSmooth{1.},blendSmooth{1.};
    std::array<std::atomic<float>*,3> matchEnabled{},original{};
    double lastDryL=0,lastDryR=0;
    uint32_t notifiedRevision=0;
    TapeEngine engine;
    TapeEngine7 engine7;
    TapeEngine8 engine8;
    std::array<std::array<std::atomic<float>*,10>,3> values{};
    std::atomic<float>* versionValue=nullptr;
    juce::ValueTree defaults;
    double sampleRate=44100;
    int activeVersion=9;
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

class TapePanel final : public juce::Component, private juce::Timer {
public:
    explicit TapePanel(TapeProcessor& p) : processor(p) {
        setLookAndFeel(&look);
        for(size_t i=0;i<knobs.size();++i) {
            auto& slider=knobs[i]; addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,115,23);
        }
        linkToggle.setButtonText("Link Input / Output");
        linkToggle.setName("Link Input / Output");
        linkToggle.setTooltip("Move Output by the opposite gain change when editing Input. Output edits set a new reference. Compensation stops at the output range limits.");
        linkToggle.setColour(juce::ToggleButton::textColourId,style::ink);
        linkToggle.setColour(juce::ToggleButton::tickColourId,style::ink);
        linkToggle.setColour(juce::ToggleButton::tickDisabledColourId,style::ink.withAlpha(.55f));
        addAndMakeVisible(linkToggle);
        for(auto* button:{&learnButton,&resetMatchButton,&enableMatchButton,&originalButton}) {
            addAndMakeVisible(*button);
            button->setColour(juce::TextButton::buttonColourId,style::ink.brighter(.15f));
            button->setColour(juce::TextButton::textColourOffId,style::cream);
        }
        learnButton.setComponentID("learnMatch"); resetMatchButton.setComponentID("resetMatch");
        enableMatchButton.setComponentID("matchEnabled"); originalButton.setComponentID("abOriginal");
        learnButton.onClick=[this] { processor.requestLearn(); refreshMatch(); };
        resetMatchButton.onClick=[this] { processor.resetMatch(); refreshMatch(); };
        enableMatchButton.setClickingTogglesState(true); originalButton.setClickingTogglesState(true);
        learnButton.setTooltip("Learn a fixed LUFS level match for the selected model from up to 15 seconds of audible audio. Finish after at least 3 seconds.");
        resetMatchButton.setTooltip("Clear only this model's learned adjustment. Tape controls, linking and A/B remain as set.");
        enableMatchButton.setTooltip("Enable or disable this model's saved level match.");
        originalButton.setTooltip("Compare original input with the processed path, using a short crossfade. Original is delayed one sample to align the plugin's fixed latency.");
        addAndMakeVisible(matchReadout); addAndMakeVisible(matchStatus);
        matchReadout.setComponentID("matchReadout"); matchStatus.setComponentID("matchStatus");
        matchReadout.setColour(juce::Label::textColourId,style::gold);
        matchReadout.setFont(juce::FontOptions(13.f)); matchReadout.setJustificationType(juce::Justification::right);
        matchStatus.setColour(juce::Label::textColourId,style::cream.withAlpha(.7f));
        matchStatus.setFont(juce::FontOptions(11.f));
        selector.setName("Tape version");
        selector.setTooltip("Choose the original ToTape7, ToTape8 or ToTape9 algorithm. Each version retains its own controls.");
        selector.addItemList({"ToTape7","ToTape8","ToTape9"},1);
        selector.setColour(juce::ComboBox::backgroundColourId,style::ink);
        selector.setColour(juce::ComboBox::textColourId,style::gold);
        selector.setColour(juce::ComboBox::outlineColourId,style::gold.withAlpha(.4f));
        addAndMakeVisible(selector);
        selectorAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,"version",selector);
        selector.onChange=[this] { if(selector.getSelectedId()>0) bindVersion(selector.getSelectedId()+6); };
        bindVersion(processor.selectedVersion());
        reset.setComponentID("resetParameters");
        reset.setButtonText("RESET TO DEFAULT"); reset.setTooltip("Reset only the selected version to the Airwindows defaults.");
        reset.onClick=[this] {
            const auto version=processor.selectedVersion();
            for(int i=0;i<controls::count(version);++i) {
                auto* parameter=processor.state.getParameter(controls::id(version,i));
                parameter->beginChangeGesture(); parameter->setValueNotifyingHost(.5f); parameter->endChangeGesture();
            }
        };
        addAndMakeVisible(reset);
        reset.setColour(juce::TextButton::buttonColourId,style::ink);
        reset.setColour(juce::TextButton::textColourOffId,style::gold);
        addAndMakeVisible(leftMeter); addAndMakeVisible(rightMeter);
        lastMeterSequence=processor.meterSequence.load(std::memory_order_acquire);
        for(int c=0;c<2;++c) {
            targets[static_cast<size_t>(c)]=processor.peaks[c].exchange(0,std::memory_order_relaxed);
            ballistics[static_cast<size_t>(c)].resetPeak(targets[static_cast<size_t>(c)]);
        }
        leftMeter.setLevelDb(ballistics[0].getLevelDb()); rightMeter.setLevelDb(ballistics[1].getLevelDb());
        lastMeterTime=juce::Time::getMillisecondCounterHiRes();
        addAndMakeVisible(sizeChoice);
        sizeChoice.setComponentID("uiSize"); sizeChoice.setName("Interface size");
        sizeChoice.setTooltip("Interface size: Small 75%, Medium 100%, or Large 125%.");
        sizeChoice.addItem("Small (75%)",1);
        sizeChoice.addItem("Medium (100%)",2);
        sizeChoice.addItem("Large (125%)",3);
        sizeChoice.setColour(juce::ComboBox::backgroundColourId,style::ink);
        sizeChoice.setColour(juce::ComboBox::textColourId,style::gold);
        sizeChoice.setColour(juce::ComboBox::outlineColourId,style::gold.withAlpha(.4f));
        sizeChoice.setSelectedId(processor.uiSize.load(),juce::dontSendNotification);
        sizeChoice.onChange=[this] {
            processor.uiSize=sizeChoice.getSelectedId();
            processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
            if(sizeChanged) sizeChanged();
        };
        setSize(980,830); startTimerHz(60);
    }
    ~TapePanel() override { stopTimer(); setLookAndFeel(nullptr); }
    std::function<void()> sizeChanged;
    void resized() override {
        sizeChoice.setBounds(750,10,180,26);
        const int top[]{0,1,2,5,8},bottom[]{3,4,6,7,9};
        for(int j=0;j<5;++j) knobs[static_cast<size_t>(top[j])].setBounds(48+j*180,370,164,150);
        const auto columns=shownVersion==7?5:4;
        for(int j=0;j<columns;++j) knobs[static_cast<size_t>(bottom[j])].setBounds((columns==5?48:138)+j*180,552,164,150);
        leftMeter.setBounds(48,139,365,182);
        rightMeter.setBounds(567,139,365,182);
        linkToggle.setBounds(330,14,200,24);
        selector.setBounds(540,42,180,30);
        reset.setBounds(750,42,180,30);
        learnButton.setBounds(48,729,140,28); resetMatchButton.setBounds(200,729,110,28);
        enableMatchButton.setBounds(322,729,110,28); originalButton.setBounds(444,729,140,28);
        matchReadout.setBounds(602,729,330,28); matchStatus.setBounds(48,764,884,20);
    }
    void paint(juce::Graphics& g) override {
        using namespace style;
        g.fillAll(cream);
        g.setColour(ink); g.setFont(juce::FontOptions(11.f));
        g.drawText("YODO AUDIO    /    AIRWINDOWS DSP",32,17,650,20,juce::Justification::left);
        g.setFont(juce::FontOptions(40.f)); g.drawText("ToTape",30,40,240,55,juce::Justification::left);
        g.setFont(juce::FontOptions(13.f)); g.drawText("S T U D I O   /   0 "+juce::String(shownVersion),248,61,300,20,juce::Justification::left);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff343d36),0,108,juce::Colour(0xff1b2420),0,790,false));
        g.fillRoundedRectangle(16,108,948,680,16);
        g.setColour(gold.withAlpha(.2f)); g.drawRoundedRectangle({17.f,109.f,946.f,678.f},15,1);
        for(float x:{30.f,950.f}) for(float y:{122.f,774.f}) {
            g.setColour(juce::Colour(0xff111a15)); g.fillEllipse(x-4,y-4,8,8);
            g.setColour(juce::Colour(0xff646c5b)); g.drawLine(x-2,y+1,x+2,y-1,1);
        }
        // Compact tape transport between the two illuminated analogue output meters.
        g.setColour(juce::Colour(0xff101713)); g.fillRoundedRectangle(426,139,128,182,10);
        g.setColour(gold); g.setFont(juce::FontOptions(11.f));
        g.drawText("ToTape"+juce::String(shownVersion),426,153,128,22,juce::Justification::centred);
        {
            juce::Graphics::ScopedSaveState transport(g);
            g.addTransform(juce::AffineTransform::scale(.36f).translated(452,215));
            drawReel(g,0,0,angle); drawReel(g,210,0,angle);
        }
        g.setColour(muted); g.setFont(juce::FontOptions(10.f));
        g.drawText("STEREO",426,256,128,18,juce::Justification::centred);
        g.drawText("SAMPLE PEAK",426,275,128,18,juce::Justification::centred);
        g.setColour(cream.withAlpha(.12f)); g.drawHorizontalLine(341,49,931);
        g.drawHorizontalLine(533,138,842);
        g.drawHorizontalLine(713,48,932);
        const int top[]{0,1,2,5,8},bottom[]{3,4,6,7,9};
        g.setColour(gold); g.setFont(juce::FontOptions(11.f));
        for(int j=0;j<5;++j) g.drawText(juce::String(controls::name(shownVersion,top[j])).toUpperCase(),48+j*180,351,164,22,juce::Justification::centred);
        const auto columns=shownVersion==7?5:4;
        for(int j=0;j<columns;++j) g.drawText(juce::String(controls::name(shownVersion,bottom[j])).toUpperCase(),(columns==5?48:138)+j*180,536,164,22,juce::Justification::centred);
        g.setColour(ink.withAlpha(.65f)); g.setFont(juce::FontOptions(11.f));
        g.drawText("ToTape"+juce::String(shownVersion)+" by Airwindows  /  Independent studio edition",32,796,710,24,juce::Justification::left);
        g.drawText("STEREO  /  FLOAT / DOUBLE",700,796,248,24,juce::Justification::right);
    }
private:
    void bindVersion(int version) {
        if(version==shownVersion) return;
        gainLinkAttachment.reset();
        enableMatchAttachment.reset(); originalAttachment.reset();
        for(auto& attachment:attachments) attachment.reset();
        shownVersion=version;
        linkToggle.setVisible(version!=7);
        for(int i=0;i<10;++i) {
            auto& slider=knobs[static_cast<size_t>(i)];
            slider.setVisible(i<controls::count(version));
            if(i>=controls::count(version)) continue;
            slider.setName(controls::name(version,i));
            slider.setTooltip(juce::String(controls::tip(version,i))+" Double-click to reset.");
            if(version==7 || (i!=0 && i!=8))
                attachments[static_cast<size_t>(i)]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,controls::id(version,i),slider);
            slider.textFromValueFunction=[i](double v) { return controls::display(i,v); };
            slider.valueFromTextFunction=[i](const juce::String& text) { return controls::parse(i,text); };
            slider.setDoubleClickReturnValue(true,.5); slider.updateText();
        }
        if(version!=7)
            gainLinkAttachment=std::make_unique<GainLinkAttachment>(
                *processor.state.getParameter(controls::id(version,0)),
                *processor.state.getParameter(controls::id(version,8)),
                *processor.state.getParameter(controls::linkId(version)),
                knobs[0],knobs[8],linkToggle);
        enableMatchAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,controls::matchId(version),enableMatchButton);
        originalAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,controls::originalId(version),originalButton);
        refreshMatch(); resized(); repaint();
    }
    void refreshMatch() {
        const auto& match=processor.matches[static_cast<size_t>(shownVersion-7)];
        const bool learned=match.learned.load();
        const bool active=processor.learning.load() && processor.learningVersion.load()==shownVersion;
        const float db=match.db.load();
        const auto trim=(db>=0?juce::String("+"):juce::String{})+juce::String(db,1)+" dB";
        matchReadout.setText(learned?"Match "+trim:"Match -- dB",juce::dontSendNotification);
        learnButton.setButtonText(active?"Finish Learning":"Learn Match");
        enableMatchButton.setButtonText(enableMatchButton.getToggleState()?"Match ON":"Match OFF");
        originalButton.setButtonText(originalButton.getToggleState()?"A: Original":"B: Processed");
        juce::String text;
        if(active) text="ToTape"+juce::String(shownVersion)+" learning: "+juce::String(processor.progress.load(),1)+" / 15 s of audio";
        else if(processor.learnResult.load()==static_cast<int>(LoudnessMatch::Result::tooShort)) text="Not enough audio. Play at least 3 seconds and retry.";
        else if(processor.learnResult.load()==static_cast<int>(LoudnessMatch::Result::noOutput)) text="No measurable tape output. Check the tape settings and retry.";
        else if(learned) {
            text="Input "+juce::String(match.inputLufs.load(),1)+" / Tape "+juce::String(match.outputLufs.load(),1)+" LUFS";
            if(std::abs(match.inputLufs.load()-match.outputLufs.load()-db)>.1f) text+=" | Gain capped at 24 dB";
            if(originalButton.getToggleState()) text+=" | Listening to original";
            else if(!enableMatchButton.getToggleState()) text+=" | Compensation disabled";
            else text+=" | Fixed gain applied";
        } else text="Play a representative section, then Learn Match for ToTape"+juce::String(shownVersion)+".";
        matchStatus.setText(text,juce::dontSendNotification);
    }
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
    void timerCallback() override {
        if(sizeChoice.getSelectedId()!=processor.uiSize.load()) {
            sizeChoice.setSelectedId(processor.uiSize.load(),juce::dontSendNotification);
            if(sizeChanged) sizeChanged();
        }
        bindVersion(processor.selectedVersion());
        refreshMatch();
        const auto now=juce::Time::getMillisecondCounterHiRes();
        const auto elapsed=(now-lastMeterTime)*.001;
        lastMeterTime=now;
        const auto sequence=processor.meterSequence.load(std::memory_order_acquire);
        if(sequence!=lastMeterSequence) {
            lastMeterSequence=sequence; idleSeconds=0;
            for(int c=0;c<2;++c) targets[static_cast<size_t>(c)]=processor.peaks[c].exchange(0,std::memory_order_relaxed);
        } else {
            idleSeconds+=elapsed;
            if(idleSeconds>processor.meterTimeoutSeconds.load(std::memory_order_relaxed)) targets.fill(0);
        }
        for(size_t c=0;c<2;++c) ballistics[c].advance(targets[c],elapsed);
        leftMeter.setLevelDb(ballistics[0].getLevelDb()); rightMeter.setLevelDb(ballistics[1].getLevelDb());
        if(juce::jmax(ballistics[0].getLevelDb(),ballistics[1].getLevelDb())>-59.95f) angle+=static_cast<float>(.75*elapsed);
        repaint(48,139,884,183);
    }
    OutputMeter leftMeter{"LEFT"},rightMeter{"RIGHT"};
    TapeProcessor& processor;
    style::Look look;
    std::array<juce::Slider,10> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,10> attachments;
    juce::ToggleButton linkToggle;
    std::unique_ptr<GainLinkAttachment> gainLinkAttachment;
    juce::ComboBox selector,sizeChoice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> selectorAttachment;
    int shownVersion=0;
    juce::TextButton reset;
    juce::TextButton learnButton{"Learn Match"},resetMatchButton{"Reset Match"},enableMatchButton{"Match ON"},originalButton{"B: Processed"};
    juce::Label matchReadout,matchStatus;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableMatchAttachment,originalAttachment;
    juce::TooltipWindow tooltip{this,600};
    std::array<MeterBallistics,2> ballistics;
    std::array<float,2> targets{};
    uint32_t lastMeterSequence=0;
    double lastMeterTime=0,idleSeconds=0;
    float angle=0;
};
// Keep logical layout and hit testing together; host/HiDPI scaling remains independent.
class TapeEditor final : public juce::AudioProcessorEditor {
public:
    explicit TapeEditor(TapeProcessor& p) : AudioProcessorEditor(p), processor(p), panel(p) {
        addAndMakeVisible(panel);
        setResizable(false,false);
        panel.sizeChanged=[this] { applySize(); };
        applySize();
    }
private:
    void applySize() {
        const float scale=processor.uiSize.load()==1?.75f:processor.uiSize.load()==3?1.25f:1.f;
        panel.setTransform(juce::AffineTransform::scale(scale));
        panel.setBounds(0,0,980,830);
        setSize(juce::roundToInt(980*scale),juce::roundToInt(830*scale));
    }
    TapeProcessor& processor;
    TapePanel panel;
};
juce::AudioProcessorEditor* TapeProcessor::createEditor() { return new TapeEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TapeProcessor; }
