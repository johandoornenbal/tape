#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/TapeControls.h"
#include "../Source/TapeEngine.h"
#include "../Source/TapeEngine7.h"
#include "../Source/TapeEngine8.h"
#include "../Source/LoudnessMatch.h"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
juce::RangedAudioParameter& param(juce::AudioProcessor& p,const juce::String& id) {
    for(auto* v:p.getParameters()) if(auto* r=dynamic_cast<juce::RangedAudioParameter*>(v);r && r->paramID==id) return *r;
    throw std::runtime_error("Missing parameter "+id.toStdString());
}
juce::ValueTree snapshot(juce::AudioProcessor& p) {
    juce::MemoryBlock bytes; p.getStateInformation(bytes);
    auto xml=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),static_cast<int>(bytes.getSize()));
    check(xml!=nullptr,"Invalid saved state"); return juce::ValueTree::fromXml(*xml);
}
void restore(juce::AudioProcessor& p,const juce::ValueTree& state) {
    auto xml=state.createXml(); juce::MemoryBlock bytes; juce::AudioProcessor::copyXmlToBinary(*xml,bytes);
    p.setStateInformation(bytes.getData(),static_cast<int>(bytes.getSize()));
}
juce::String key(int version,const char* property) { return "v"+juce::String(version)+"_"+property; }
struct UI {
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    explicit UI(juce::AudioProcessor& p):editor(p.createEditor()) {}
    juce::Component& component(const char* id) {
        for(auto* c:editor->getChildComponent(0)->getChildren()) if(c->getComponentID()==id) return *c;
        throw std::runtime_error("Missing UI component");
    }
    void click(const char* id) { dynamic_cast<juce::TextButton&>(component(id)).onClick(); }
    void select(int version) {
        for(auto* c:editor->getChildComponent(0)->getChildren()) if(auto* combo=dynamic_cast<juce::ComboBox*>(c);combo && combo->getName()=="Tape version") combo->setSelectedId(version-6,juce::sendNotificationSync);
    }
    void refresh(int version) { select(version==7?8:7); select(version); }
};
void fill(juce::AudioBuffer<double>& b,int base) {
    for(int i=0;i<b.getNumSamples();++i) {
        const auto t=static_cast<double>(base+i)/48000.;
        b.setSample(0,i,.11*std::sin(t*2*3.141592653589793*997)+.03*std::sin(t*2*3.141592653589793*213));
        b.setSample(1,i,.07*std::sin(t*2*3.141592653589793*521));
    }
}
template<class Engine> void learn(juce::AudioProcessor& p,UI& ui,int version) {
    ui.select(version);
    param(p,controls::originalId(version)).setValueNotifyingHost(0);
    p.setProcessingPrecision(juce::AudioProcessor::doublePrecision);
    p.setRateAndBufferSizeDetails(48000,256); p.prepareToPlay(48000,256);
    Engine reference; reference.reset(48000);
    for(int i=0;i<controls::count(version);++i) reference.setParameter(i,param(p,controls::id(version,i)).getValue());
    LoudnessMatch expected; expected.prepare(48000); expected.start();
    juce::AudioBuffer<double> b(2,256),wet(2,256); juce::MidiBuffer midi;
    ui.click("learnMatch");
    int base=0;
    for(;base<48000*4;base+=256) {
        fill(b,base); wet.makeCopyOf(b);
        double* data[]{wet.getWritePointer(0),wet.getWritePointer(1)};
        reference.processDoubleReplacing(data,data,256);
        for(int i=0;i<256;++i) expected.sample(b.getSample(0,i),b.getSample(1,i),wet.getSample(0,i),wet.getSample(1,i));
        p.processBlock(b,midi);
        for(int c=0;c<2;++c) for(int i=0;i<256;++i) check(b.getSample(c,i)==wet.getSample(c,i),"Learning changes untrimmed engine output");
    }
    check(expected.finish()==LoudnessMatch::Result::success,"Reference passage not learnable");
    ui.click("learnMatch"); // Finish after four seconds, using the real audio-thread command.
    for(int block=0;block<8;++block,base+=256) {
        fill(b,base); wet.makeCopyOf(b);
        double* data[]{wet.getWritePointer(0),wet.getWritePointer(1)}; reference.processDoubleReplacing(data,data,256);
        p.processBlock(b,midi);
        if(block==7) {
            auto saved=snapshot(p);
            const auto db=static_cast<float>(saved.getProperty(key(version,"matchDb")));
            check(static_cast<bool>(saved.getProperty(key(version,"hasMatch"))),"Learned match not saved");
            check(std::abs(db-expected.trimDb)<1.e-4,"Learned trim differs from original-engine measurement");
            check(std::abs(static_cast<double>(saved.getProperty(key(version,"inputLufs")))-expected.inputLufs)<1.e-4,"Measured input LUFS wrong");
            const auto gain=std::pow(10.,static_cast<double>(db)/20.);
            for(int c=0;c<2;++c) for(int i=0;i<256;++i) check(std::abs(b.getSample(c,i)-wet.getSample(c,i)*gain)<1.e-12,"Gain is not applied exactly once after the engine");
            std::cout<<"ToTape"<<version<<": learned "<<db<<" dB, expected "<<expected.trimDb<<'\n';
        }
    }
    ui.refresh(version);
    auto& label=dynamic_cast<juce::Label&>(ui.component("matchReadout"));
    check(label.getText().contains("dB") && !label.getText().contains("--"),"Learned readout missing");
    check(label.getFont().getHeight()<=14,"Applied gain readout too prominent");
    for(const char* id:{"learnMatch","resetMatch","matchEnabled","abOriginal","matchReadout","matchStatus"})
        check(ui.editor->getLocalBounds().contains(ui.component(id).getBounds()),"Matching controls outside editor");
    p.releaseResources();
}
template<class Engine> void verifyAppliedAfterSwitch(juce::AudioProcessor& p,int version) {
    param(p,"version").setValueNotifyingHost(static_cast<float>(version-7)*.5f);
    Engine reference; reference.reset(48000);
    for(int i=0;i<controls::count(version);++i) reference.setParameter(i,param(p,controls::id(version,i)).getValue());
    juce::AudioBuffer<double> b(2,256),wet(2,256); juce::MidiBuffer midi;
    const auto saved=snapshot(p);
    const auto db=static_cast<double>(saved.getProperty(key(version,"matchDb")));
    const auto gain=param(p,controls::matchId(version)).getValue()>.5f?std::pow(10.,db/20.):1.;
    for(int block=0;block<8;++block) {
        fill(b,block*256); wet.makeCopyOf(b);
        double* data[]{wet.getWritePointer(0),wet.getWritePointer(1)};
        reference.processDoubleReplacing(data,data,256); p.processBlock(b,midi);
        if(block==7) for(int c=0;c<2;++c) for(int i=0;i<256;++i)
            check(std::abs(b.getSample(c,i)-wet.getSample(c,i)*gain)<1.e-12,"Model switch did not apply that model's learned adjustment/enable setting");
    }
}
template<class T> void original(juce::AudioProcessor& p,int version) {
    param(p,"version").setValueNotifyingHost(static_cast<float>(version-7)*.5f);
    param(p,controls::originalId(version)).setValueNotifyingHost(1);
    p.setProcessingPrecision(std::is_same_v<T,float>?juce::AudioProcessor::singlePrecision:juce::AudioProcessor::doublePrecision);
    p.prepareToPlay(48000,37);
    juce::AudioBuffer<T> b(2,37); juce::MidiBuffer midi;
    T previous[2]{};
    for(int block=0;block<8;++block) {
        T next[2]{};
        for(int c=0;c<2;++c) for(int i=0;i<37;++i) b.setSample(c,i,static_cast<T>(.15*std::sin((block*37+i)*.07*(c+1))));
        juce::AudioBuffer<T> dry; dry.makeCopyOf(b);
        p.processBlock(b,midi);
        for(int c=0;c<2;++c) {
            for(int i=0;i<37;++i) check(b.getSample(c,i)==(i==0?previous[c]:dry.getSample(c,i-1)),"Original A/B is not dry input aligned by one sample");
            next[c]=dry.getSample(c,36);
        }
        previous[0]=next[0]; previous[1]=next[1];
    }
    p.releaseResources();
}
int main() try {
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter()); UI ui(*p);
    // Make the three learned values deliberately different.
    param(*p,controls::id(7,2)).setValueNotifyingHost(.2f);
    param(*p,controls::id(8,8)).setValueNotifyingHost(.25f);
    param(*p,controls::id(9,8)).setValueNotifyingHost(.75f);
    learn<TapeEngine7>(*p,ui,7); learn<TapeEngine8>(*p,ui,8); learn<TapeEngine>(*p,ui,9);
    const auto learned=snapshot(*p);
    const auto db7=static_cast<double>(learned.getProperty(key(7,"matchDb")));
    const auto db8=static_cast<double>(learned.getProperty(key(8,"matchDb")));
    const auto db9=static_cast<double>(learned.getProperty(key(9,"matchDb")));
    check(std::abs(db7-db8)>1 && std::abs(db8-db9)>1,"Model matches were shared");
    // Relearning with an existing adjustment must measure before compensation,
    // otherwise it would incorrectly learn approximately 0 dB on the second pass.
    ui.select(8); p->prepareToPlay(48000,256); ui.click("learnMatch");
    juce::AudioBuffer<double> repeat(2,256); juce::MidiBuffer repeatMidi;
    for(int base=0;base<48000*4;base+=256) { fill(repeat,base); p->processBlock(repeat,repeatMidi); }
    ui.click("learnMatch"); fill(repeat,0); p->processBlock(repeat,repeatMidi);
    check(std::abs(static_cast<double>(snapshot(*p).getProperty(key(8,"matchDb")))-db8)<1.e-4,"Relearning feeds compensated output into analysis");
    p->releaseResources();
    param(*p,controls::matchId(8)).setValueNotifyingHost(0);
    ui.select(7); p->prepareToPlay(48000,256);
    verifyAppliedAfterSwitch<TapeEngine8>(*p,8); verifyAppliedAfterSwitch<TapeEngine>(*p,9);
    verifyAppliedAfterSwitch<TapeEngine7>(*p,7); verifyAppliedAfterSwitch<TapeEngine8>(*p,8);
    verifyAppliedAfterSwitch<TapeEngine>(*p,9); p->releaseResources();
    for(int version:{7,8,9}) { original<float>(*p,version); original<double>(*p,version); }
    param(*p,controls::matchId(7)).setValueNotifyingHost(0);
    param(*p,controls::matchId(8)).setValueNotifyingHost(1);
    param(*p,controls::matchId(9)).setValueNotifyingHost(0);
    param(*p,controls::originalId(8)).setValueNotifyingHost(0);
    for(int version:{7,8,9,8,7,9}) {
        ui.select(version);
        auto& button=dynamic_cast<juce::TextButton&>(ui.component("matchEnabled"));
        check(button.getToggleState()==(version==8),"Switch did not restore model's Match ON/OFF setting");
        auto& ab=dynamic_cast<juce::TextButton&>(ui.component("abOriginal"));
        check(ab.getToggleState()==(version!=8),"Switch did not restore model's Original A/B setting");
        auto saved=snapshot(*p);
        auto text=dynamic_cast<juce::Label&>(ui.component("matchReadout")).getText();
        const auto db=static_cast<double>(saved.getProperty(key(version,"matchDb")));
        check(text.contains(juce::String(db,1)),"Switch did not display this model's learned trim");
    }
    juce::MemoryBlock bytes; p->getStateInformation(bytes);
    std::unique_ptr<juce::AudioProcessor> restored(createPluginFilter());
    restored->setStateInformation(bytes.getData(),static_cast<int>(bytes.getSize()));
    const auto recalled=snapshot(*restored),before=snapshot(*p);
    for(int version:{7,8,9}) {
        check(recalled.getProperty(key(version,"matchDb"))==before.getProperty(key(version,"matchDb")),"Recall lost a learned trim");
        check(static_cast<bool>(recalled.getProperty(key(version,"hasMatch"))),"Recall lost model's learned flag");
        check(param(*restored,controls::matchId(version)).getValue()==param(*p,controls::matchId(version)).getValue(),"Recall lost per-model enable");
        check(param(*restored,controls::originalId(version)).getValue()==param(*p,controls::originalId(version)).getValue(),"Recall lost per-model A/B");
    }
    // Reset affects one learned bank, not tape controls or preferences.
    ui.select(8); const auto tapeOutput=param(*p,controls::id(8,8)).getValue();
    ui.click("resetMatch");
    const auto reset=snapshot(*p);
    check(!static_cast<bool>(reset.getProperty(key(8,"hasMatch"))),"Reset did not clear selected model");
    check(static_cast<bool>(reset.getProperty(key(7,"hasMatch"))) && static_cast<bool>(reset.getProperty(key(9,"hasMatch"))),"Reset cleared other models");
    check(param(*p,controls::id(8,8)).getValue()==tapeOutput && param(*p,controls::matchId(8)).getValue()>.5f,"Reset changed tape controls or preferences");
    // A model change during learning cancels rather than combining two engines.
    ui.select(7); param(*p,controls::originalId(7)).setValueNotifyingHost(0);
    p->setProcessingPrecision(juce::AudioProcessor::doublePrecision); p->prepareToPlay(48000,256);
    ui.click("learnMatch"); juce::AudioBuffer<double> b(2,256); juce::MidiBuffer midi;
    for(int n=0;n<220;++n) { fill(b,n*256); p->processBlock(b,midi); }
    ui.select(9); fill(b,0); p->processBlock(b,midi); ui.refresh(9);
    check(dynamic_cast<juce::TextButton&>(ui.component("learnMatch")).getButtonText()=="Learn Match","Changing model did not cancel learning");
    check(snapshot(*p).getProperty(key(7,"matchDb"))==before.getProperty(key(7,"matchDb")),"Canceled learn replaced last successful match");
    // Host bypass must not count as learning time.
    ui.click("learnMatch");
    for(int n=0;n<220;++n) { fill(b,n*256); p->processBlock(b,midi); }
    for(int n=0;n<700;++n) { fill(b,n*256); p->processBlockBypassed(b,midi); }
    ui.click("learnMatch"); fill(b,0); p->processBlock(b,midi); ui.refresh(9);
    check(dynamic_cast<juce::Label&>(ui.component("matchStatus")).getText().contains("Not enough audio"),"Bypassed audio counted toward learning");
    // Legacy sessions lack the appended controls and learned data; no gain is applied.
    auto legacy=before.createCopy();
    for(int i=legacy.getNumChildren();--i>=0;) {
        auto id=legacy.getChild(i).getProperty("id").toString();
        if(id.endsWith("_matchEnabled") || id.endsWith("_original")) legacy.removeChild(i,nullptr);
    }
    for(int version:{7,8,9}) for(const char* property:{"matchDb","hasMatch","inputLufs","outputLufs"}) legacy.removeProperty(key(version,property),nullptr);
    restore(*restored,legacy);
    for(int version:{7,8,9}) {
        check(!static_cast<bool>(snapshot(*restored).getProperty(key(version,"hasMatch"))),"Legacy session has a learned adjustment");
        check(param(*restored,controls::originalId(version)).getValue()<.5f && param(*restored,controls::matchId(version)).getValue()>.5f,"Legacy matching defaults wrong");
    }
    std::cout<<"Per-model learning, post-engine compensation, Original A/B, recall, reset isolation, switch cancellation, bypass pause and legacy defaults passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
