#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    try {
        juce::ScopedJuceInitialiser_GUI init;
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        require(p->getParameters().size()==9 && p->hasEditor(),"Missing parameters/editor");
        juce::MemoryBlock saved;
        for(int i=0;i<9;++i) {
            require(std::abs(p->getParameters()[i]->getValue()-.5f)<1.e-6f,"Incorrect default");
            p->getParameters()[i]->setValueNotifyingHost(static_cast<float>(i+1)/11.f);
        }
        p->getStateInformation(saved);
        for(auto* v:p->getParameters()) v->setValueNotifyingHost(.5f);
        p->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
        for(int i=0;i<9;++i) require(std::abs(p->getParameters()[i]->getValue()-static_cast<float>(i+1)/11.f)<1.e-6f,"State recall failed");
        for(double rate:{44100.,48000.,96000.}) {
            p->setRateAndBufferSizeDetails(rate,512); p->prepareToPlay(rate,512);
            require(p->getLatencySamples()==1,"Unexpected latency");
            juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi;
            for(int i=0;i<512;++i) { audio.setSample(0,i,.4f*std::sin(i*.05f)); audio.setSample(1,i,.2f*std::sin(i*.07f)); }
            p->processBlock(audio,midi);
            require(std::isfinite(audio.getMagnitude(0,512)) && audio.getMagnitude(0,512)>.01f,"Float audio failed");
            p->releaseResources();
            p->setProcessingPrecision(juce::AudioProcessor::doublePrecision);
            p->prepareToPlay(rate,512);
            juce::AudioBuffer<double> precise(2,512);
            for(int c=0;c<2;++c) for(int i=0;i<512;++i) precise.setSample(c,i,.3*std::sin(i*.05));
            p->processBlock(precise,midi);
            require(std::isfinite(precise.getMagnitude(0,512)) && precise.getMagnitude(0,512)>.01,"Double audio failed");
            p->releaseResources();
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        require(editor->getWidth()==980 && editor->getHeight()==760,"Wrong editor size");
        int sliders=0;
        for(auto* child:editor->getChildren()) if(auto* s=dynamic_cast<juce::Slider*>(child)) {
            require(editor->getLocalBounds().contains(s->getBounds()),"Control outside editor");
            require(s->getName().isNotEmpty(),"Unnamed control");
            const auto value=.37; s->setValue(value,juce::sendNotificationSync);
            require(std::abs(p->getParameters()[sliders]->getValue()-.37f)<1.e-6f,"Slider binding failed");
            ++sliders;
        }
        require(sliders==9,"Missing sliders");
        for(auto* child:editor->getChildren()) if(auto* button=dynamic_cast<juce::TextButton*>(child)) button->onClick();
        for(auto* v:p->getParameters()) require(std::abs(v->getValue()-.5f)<1.e-6f,"Reset failed");
        for(auto* child:editor->getChildren()) if(auto* slider=dynamic_cast<juce::Slider*>(child)) {
            require(slider->getTextFromValue(slider->getValue())==(slider->getName()=="Head frequency"?"68.8 Hz":"50.0 %"),"Incorrect reset value display");
        }
        juce::Image image(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);
        juce::Graphics g(image); editor->paintEntireComponent(g,true);
        auto target=juce::File::getCurrentWorkingDirectory().getChildFile("ui-preview.png");
        auto out=target.createOutputStream(); require(out!=nullptr,"Cannot save preview");
        out->setPosition(0); out->truncate(); require(juce::PNGImageFormat().writeImageToStream(image,*out),"PNG render failed"); out.reset();
        std::cout<<"Nine parameter bindings, defaults/reset, state recall, float/double audio at three rates, latency and native editor render passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
