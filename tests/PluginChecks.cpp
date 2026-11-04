#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/TapeControls.h"
#include "../Source/TapeEngine.h"
#include "../Source/TapeEngine7.h"
#include "../Source/TapeEngine8.h"
#include "../Source/OutputMeter.h"
#include "../Source/MeterBallistics.h"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
juce::RangedAudioParameter& parameter(juce::AudioProcessor& p,const juce::String& id) {
    for(auto* value:p.getParameters()) if(auto* v=dynamic_cast<juce::RangedAudioParameter*>(value);v && v->paramID==id) return *v;
    throw std::runtime_error("Parameter not found: "+id.toStdString());
}
void select(juce::AudioProcessor& p,int version) { parameter(p,"version").setValueNotifyingHost(static_cast<float>(version-7)*.5f); }
template<class Engine,class T> void compare(juce::AudioProcessor& p,int version,double rate) {
    select(p,version);
    p.setProcessingPrecision(std::is_same_v<T,float>?juce::AudioProcessor::singlePrecision:juce::AudioProcessor::doublePrecision);
    p.setRateAndBufferSizeDetails(rate,257); p.prepareToPlay(rate,257);
    Engine reference; reference.reset(rate);
    for(int i=0;i<controls::count(version);++i) {
        auto value=static_cast<float>((i*3+4)%11)*.1f;
        parameter(p,controls::id(version,i)).setValueNotifyingHost(value); reference.setParameter(i,value);
    }
    juce::MidiBuffer midi; juce::AudioBuffer<T> audio(2,257),expected(2,257);
    for(int block=0;block<20;++block) {
        for(int c=0;c<2;++c) for(int i=0;i<257;++i) {
            const auto value=static_cast<T>(.4*std::sin((block*257+i)*.05*(c+1)));
            audio.setSample(c,i,value); expected.setSample(c,i,value);
        }
        p.processBlock(audio,midi);
        T* ptr[]{expected.getWritePointer(0),expected.getWritePointer(1)};
        if constexpr(std::is_same_v<T,float>) reference.processReplacing(ptr,ptr,257);
        else reference.processDoubleReplacing(ptr,ptr,257);
        for(int c=0;c<2;++c) for(int i=0;i<257;++i)
            require(std::isfinite(audio.getSample(c,i)) && audio.getSample(c,i)==expected.getSample(c,i),"Wrapper routing changed engine output");
    }
    require(p.getLatencySamples()==1,"Wrong reported latency");
    p.releaseResources();
}
void savePreview(juce::AudioProcessorEditor& editor,int version) {
    juce::Image image(juce::Image::ARGB,editor.getWidth(),editor.getHeight(),true);
    juce::Graphics g(image); editor.paintEntireComponent(g,true);
    auto name=version==9?juce::String("ui-preview.png"):"ui-preview-"+juce::String(version)+".png";
    auto out=juce::File::getCurrentWorkingDirectory().getChildFile(name).createOutputStream();
    require(out!=nullptr,"Cannot save preview"); out->setPosition(0); out->truncate();
    require(juce::PNGImageFormat().writeImageToStream(image,*out),"PNG render failed");
}
void checkMeterDamping() {
    MeterBallistics meter;
    require(meter.advance(1.f,1.0/60)<-40.f,"Needle jumps immediately on a peak");
    for(int i=0;i<90;++i) meter.advance(.5f,1.0/60);
    require(std::abs(meter.getLevelDb()+6.0206f)<.05f,"Steady-level meter calibration changed");
    const auto before=meter.getLevelDb();
    meter.advance(0,1.0/60);
    require(meter.getLevelDb()<before && meter.getLevelDb()>before-3.f,"Needle release is abrupt");
    for(int i=0;i<360;++i) meter.advance(0,1.0/60);
    require(meter.getLevelDb()<=-60.f,"Stopped meter does not return to rest");
    // Equal elapsed time must give the same response despite UI timer jitter.
    MeterBallistics regular,jittered;
    for(int i=0;i<60;++i) regular.advance(.5f,1.0/60);
    for(int i=0;i<20;++i) { jittered.advance(.5f,.01); jittered.advance(.5f,.04); }
    require(std::abs(regular.getLevelDb()-jittered.getLevelDb())<1.e-4f,"Damping depends on timer cadence");
    // Alternating peaks should produce much less needle swing than their raw 12 dB range.
    MeterBallistics fluctuating;
    fluctuating.resetPeak(.5f);
    float low=0,high=-60;
    for(int i=0;i<240;++i) {
        auto db=fluctuating.advance(i%2==0?.25f:1.f,1.0/60);
        if(i>120) { low=std::min(low,db); high=std::max(high,db); }
    }
    require(high-low<2.f,"Rapid peaks are not averaged enough");
}
struct HostChangeObserver final : juce::AudioProcessorParameter::Listener {
    int values=0,begins=0,ends=0;
    void parameterValueChanged(int,float) override { ++values; }
    void parameterGestureChanged(int,bool starting) override { if(starting) ++begins; else ++ends; }
};
void checkGainLink() {
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    juce::ComboBox* selector=nullptr; juce::ToggleButton* link=nullptr;
    for(auto* child:editor->getChildren()) {
        if(auto* combo=dynamic_cast<juce::ComboBox*>(child)) selector=combo;
        if(auto* toggle=dynamic_cast<juce::ToggleButton*>(child)) link=toggle;
    }
    require(selector && link,"Missing input/output link control");
    auto slider=[&](const juce::String& name) {
        for(auto* child:editor->getChildren()) if(auto* v=dynamic_cast<juce::Slider*>(child);v && v->getName()==name) return v;
        throw std::runtime_error("Slider not found");
    };
    auto close=[](double a,double b) { return std::abs(a-b)<1.e-6; };
    for(int version:{8,9}) {
        selector->setSelectedId(version-6,juce::sendNotificationSync);
        require(link->isVisible() && !link->getToggleState(),"Link must default to off separately for each version");
        require(editor->getLocalBounds().contains(link->getBounds()),"Link outside editor");
        auto* input=slider("Input"); auto* output=slider("Output");
        input->setValue(.5,juce::sendNotificationSync); output->setValue(.5,juce::sendNotificationSync);
        link->setToggleState(true,juce::sendNotificationSync);
        require(parameter(*p,controls::linkId(version)).getValue()>.5f,"Link toggle not saved as a parameter");
        require(close(input->getValue(),.5) && close(output->getValue(),.5),"Enabling link changes the gain");
        HostChangeObserver inputEvents,outputEvents;
        auto& inputParameter=parameter(*p,controls::id(version,0));
        auto& outputParameter=parameter(*p,controls::id(version,8));
        inputParameter.addListener(&inputEvents); outputParameter.addListener(&outputEvents);
        input->setValue(1,juce::sendNotificationSync);
        inputParameter.removeListener(&inputEvents); outputParameter.removeListener(&outputEvents);
        require(inputEvents.values>0 && outputEvents.values>0 && inputEvents.begins==1 && inputEvents.ends==1 && outputEvents.begins==1 && outputEvents.ends==1,"Linked edits not reported as balanced host gestures");
        require(close(output->getValue(),.125),"Linked output does not compensate squared input gain");
        require(close(parameter(*p,controls::id(version,8)).getValue(),.125),"Compensated output not sent to host");
        input->setValue(.25,juce::sendNotificationSync);
        require(close(output->getValue(),1),"Output range not respected");
        input->setValue(.5,juce::sendNotificationSync);
        require(close(output->getValue(),.5),"Clamping lost the original reference");
        input->setValue(input->getValueFromText("75 %"),juce::sendNotificationSync);
        require(close(output->getValue(),2.0/9.0),"Typed input value is not gain linked");
        // The same value path is used by the configured double-click reset.
        input->setValue(.5,juce::sendNotificationSync);
        require(close(output->getValue(),.5),"Input reset is not gain linked");
        output->setValue(.25,juce::sendNotificationSync);
        input->setValue(1,juce::sendNotificationSync);
        require(close(output->getValue(),.0625),"Manual output edit did not set a new reference");
        // Automation/state changes must not generate unintended paired writes.
        parameter(*p,controls::id(version,0)).setValueNotifyingHost(.75f);
        require(close(output->getValue(),.0625),"Input automation rewrites output automation");
        input->setValue(.5,juce::sendNotificationSync);
        require(close(output->getValue(),.140625),"UI editing after automation uses a stale reference");
        link->setToggleState(false,juce::sendNotificationSync);
        input->setValue(.8,juce::sendNotificationSync);
        require(close(output->getValue(),.140625),"Unlinked input changes output");
        input->setValue(0,juce::sendNotificationSync); output->setValue(.5,juce::sendNotificationSync);
        link->setToggleState(true,juce::sendNotificationSync);
        input->setValue(.5,juce::sendNotificationSync);
        require(close(output->getValue(),.5),"Linking at mute is not stable");
        input->setValue(0,juce::sendNotificationSync);
        require(close(output->getValue(),1),"Muted input does not clamp safely");
        input->setValue(.5,juce::sendNotificationSync);
        require(close(output->getValue(),.5),"Recovery from muted input lost the reference");
        output->setValue(0,juce::sendNotificationSync); input->setValue(1,juce::sendNotificationSync);
        require(close(output->getValue(),0),"Output mute not preserved");
        output->setValue(.3,juce::sendNotificationSync); input->setValue(.5,juce::sendNotificationSync);
        require(close(output->getValue(),1),"Manual output reanchoring does not clamp");
        input->setValue(1,juce::sendNotificationSync);
        require(close(output->getValue(),.3),"Manual output reference not retained after clamping");
    }
    selector->setSelectedId(1,juce::sendNotificationSync);
    require(!link->isVisible(),"ToTape7 must not expose input/output linking");
    selector->setSelectedId(3,juce::sendNotificationSync);
    require(link->getToggleState(),"Link preference lost on version switch");
    juce::MemoryBlock saved; p->getStateInformation(saved);
    // Restore with the editor open, so attachment callbacks are exercised too.
    parameter(*p,controls::linkId(8)).setValueNotifyingHost(0);
    parameter(*p,controls::linkId(9)).setValueNotifyingHost(0);
    parameter(*p,"input").setValueNotifyingHost(.2f);
    p->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
    require(parameter(*p,controls::linkId(8)).getValue()>.5f && parameter(*p,controls::linkId(9)).getValue()>.5f,"Link states not recalled independently");
    require(close(parameter(*p,"input").getValue(),1) && close(parameter(*p,"output").getValue(),.3),"Recall applies gain compensation a second time");
    // Migrate a previous multi-version state that predates the link parameters.
    auto xml=juce::AudioProcessor::getXmlFromBinary(saved.getData(),static_cast<int>(saved.getSize()));
    auto old=juce::ValueTree::fromXml(*xml);
    for(int i=old.getNumChildren();--i>=0;) if(old.getChild(i).getProperty("id").toString().endsWith("_linkIO")) old.removeChild(i,nullptr);
    juce::MemoryBlock oldBytes; auto oldXml=old.createXml(); juce::AudioProcessor::copyXmlToBinary(*oldXml,oldBytes);
    p->setStateInformation(oldBytes.getData(),static_cast<int>(oldBytes.getSize()));
    require(parameter(*p,controls::linkId(8)).getValue()<.5f && parameter(*p,controls::linkId(9)).getValue()<.5f,"Old sessions must restore with linking off");
    parameter(*p,controls::linkId(8)).setValueNotifyingHost(1);
    parameter(*p,controls::linkId(9)).setValueNotifyingHost(1);
    compare<TapeEngine8,float>(*p,8,48000); compare<TapeEngine8,double>(*p,8,48000);
    compare<TapeEngine,float>(*p,9,48000); compare<TapeEngine,double>(*p,9,48000);
}
int main() {
    try {
        checkMeterDamping();
        juce::ScopedJuceInitialiser_GUI init;
        checkGainLink();
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        require(p->getParameters().size()==31 && p->hasEditor(),"Missing parameter banks/editor");
        require(parameter(*p,"version").getValue()==1.f,"Default must remain ToTape9");
        for(int i=0;i<9;++i) {
            require(dynamic_cast<juce::AudioProcessorParameterWithID*>(p->getParameters()[i])->paramID==controls::id(9,i),"Legacy parameter order changed");
        }
        for(double rate:{44100.,48000.,96000.,192000.}) {
            compare<TapeEngine7,float>(*p,7,rate); compare<TapeEngine7,double>(*p,7,rate);
            compare<TapeEngine8,float>(*p,8,rate); compare<TapeEngine8,double>(*p,8,rate);
            compare<TapeEngine,float>(*p,9,rate); compare<TapeEngine,double>(*p,9,rate);
        }
        // Check switching during an already prepared stream resets the entering engine.
        p->setProcessingPrecision(juce::AudioProcessor::doublePrecision);
        p->prepareToPlay(48000,64);
        juce::MidiBuffer midi; juce::AudioBuffer<double> switched(2,64);
        for(int version:{9,7,8,7,9}) {
            select(*p,version);
            for(int c=0;c<2;++c) for(int i=0;i<64;++i) switched.setSample(c,i,.2*std::sin(i*.08));
            juce::AudioBuffer<double> expected; expected.makeCopyOf(switched);
            auto expectedFromFreshEngine=[&](auto& engine) {
                engine.reset(48000);
                for(int i=0;i<controls::count(version);++i) engine.setParameter(i,parameter(*p,controls::id(version,i)).getValue());
                double* data[]{expected.getWritePointer(0),expected.getWritePointer(1)};
                engine.processDoubleReplacing(data,data,64);
            };
            if(version==7) { TapeEngine7 engine; expectedFromFreshEngine(engine); }
            else if(version==8) { TapeEngine8 engine; expectedFromFreshEngine(engine); }
            else { TapeEngine engine; expectedFromFreshEngine(engine); }
            p->processBlock(switched,midi);
            for(int c=0;c<2;++c) for(int i=0;i<64;++i)
                require(switched.getSample(c,i)==expected.getSample(c,i),"Switch did not route to a freshly reset original engine");
        }
        p->releaseResources();
        // Publish unequal real output peaks before opening the editor.
        p->prepareToPlay(48000,64);
        juce::AudioBuffer<double> meterAudio(2,64);
        for(int i=0;i<64;++i) { meterAudio.setSample(0,i,.35*std::sin(i*.08)); meterAudio.setSample(1,i,.07*std::sin(i*.08)); }
        p->processBlock(meterAudio,midi);
        const float expectedMeterDb[]{
            juce::Decibels::gainToDecibels(static_cast<float>(meterAudio.getMagnitude(0,0,64)),-60.f),
            juce::Decibels::gainToDecibels(static_cast<float>(meterAudio.getMagnitude(1,0,64)),-60.f)};
        // Actual UI selector, independent bank bindings, and selected-bank reset.
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        require(editor->getWidth()==980 && editor->getHeight()==760,"Wrong editor size");
        int meterCount=0;
        for(auto* child:editor->getChildren()) if(auto* meter=dynamic_cast<OutputMeter*>(child)) {
            const int channel=meter->getTitle().endsWith("LEFT")?0:1;
            require(editor->getLocalBounds().contains(meter->getBounds()),"Meter outside editor");
            require(std::abs(meter->getLevelDb()-expectedMeterDb[channel])<1.e-5f,"Analogue meter does not match actual channel output");
            ++meterCount;
        }
        require(meterCount==2,"Missing stereo analogue meters");
        p->releaseResources();
        juce::ComboBox* selector=nullptr; juce::TextButton* reset=nullptr;
        for(auto* child:editor->getChildren()) {
            if(auto* combo=dynamic_cast<juce::ComboBox*>(child)) selector=combo;
            if(auto* button=dynamic_cast<juce::TextButton*>(child)) reset=button;
        }
        require(selector && reset && selector->getNumItems()==3,"Missing version selector/reset");
        for(int version:{7,8,9}) {
            selector->setSelectedId(version-6,juce::sendNotificationSync);
            require(parameter(*p,"version").getValue()==static_cast<float>(version-7)*.5f,"Selector not bound");
            int count=0;
            for(auto* child:editor->getChildren()) if(auto* slider=dynamic_cast<juce::Slider*>(child);slider && slider->isVisible()) {
                require(editor->getLocalBounds().contains(slider->getBounds()),"Control outside editor");
                require(slider->getName()==controls::name(version,count),"Wrong version control");
                slider->setValue(.37,juce::sendNotificationSync);
                require(std::abs(parameter(*p,controls::id(version,count)).getValue()-.37f)<1.e-6f,"Wrong bank binding");
                ++count;
            }
            require(count==controls::count(version),"Incorrect visible control count");
            reset->onClick();
            for(int i=0;i<count;++i) require(parameter(*p,controls::id(version,i)).getValue()==.5f,"Reset failed");
            for(auto* child:editor->getChildren()) if(auto* slider=dynamic_cast<juce::Slider*>(child);slider && slider->isVisible())
                require(slider->getTextFromValue(slider->getValue())==(slider->getName()=="Head frequency"?"68.8 Hz":"50.0 %"),"Wrong displayed default");
            savePreview(*editor,version);
            parameter(*p,controls::id(version,0)).setValueNotifyingHost(static_cast<float>(version)*.1f);
        }
        for(int version:{7,8,9}) require(std::abs(parameter(*p,controls::id(version,0)).getValue()-static_cast<float>(version)*.1f)<1.e-6f,"Switch/reset affected another bank");
        select(*p,8);
        juce::MemoryBlock saved; p->getStateInformation(saved);
        std::unique_ptr<juce::AudioProcessor> restored(createPluginFilter());
        restored->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
        for(int i=0;i<31;++i) require(restored->getParameters()[i]->getValue()==p->getParameters()[i]->getValue(),"State recall lost version or bank values");
        // A real legacy-shaped state must select ToTape9 even over a current ToTape7 session.
        auto xml=juce::AudioProcessor::getXmlFromBinary(saved.getData(),static_cast<int>(saved.getSize()));
        auto legacy=juce::ValueTree::fromXml(*xml);
        for(int i=legacy.getNumChildren();--i>=0;) {
            auto id=legacy.getChild(i).getProperty("id").toString();
            if(id=="version" || id.startsWith("v7_") || id.startsWith("v8_") || id.endsWith("_linkIO")) legacy.removeChild(i,nullptr);
        }
        juce::MemoryBlock legacyBytes; auto legacyXml=legacy.createXml();
        juce::AudioProcessor::copyXmlToBinary(*legacyXml,legacyBytes);
        select(*restored,7);
        restored->setStateInformation(legacyBytes.getData(),static_cast<int>(legacyBytes.getSize()));
        require(parameter(*restored,"version").getValue()==1.f,"Legacy state did not select ToTape9");
        require(parameter(*restored,"input").getValue()==parameter(*p,"input").getValue(),"Legacy values changed");
        require(parameter(*restored,"v7_0").getValue()==.5f && parameter(*restored,"v8_0").getValue()==.5f,"Legacy bank defaults wrong");
        std::cout<<"All three versions: exact float/double wrapper routing at four rates, switching, selector/control bindings, independent bank reset/recall, legacy state migration and editor renders passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
