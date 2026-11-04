#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "InputOutputLink.h"

// Couple editor gestures, not parameter callbacks. This records both parameters
// in the host and prevents state recall/automation from writing other parameters.
class GainLinkAttachment final : private juce::Slider::Listener, private juce::Button::Listener {
public:
    GainLinkAttachment(juce::RangedAudioParameter& inputParameter,
                       juce::RangedAudioParameter& outputParameter,
                       juce::RangedAudioParameter& linkParameter,
                       juce::Slider& inputSlider,juce::Slider& outputSlider,juce::Button& linkButton)
        : inputValue(inputParameter),outputValue(outputParameter),linkValue(linkParameter),
          input(inputSlider),output(outputSlider),button(linkButton),
          inputAttachment(inputParameter,[this](float value) { updateSlider(input,value); },nullptr),
          outputAttachment(outputParameter,[this](float value) { updateSlider(output,value); },nullptr),
          linkAttachment(linkParameter,[this](float value) {
              juce::ScopedValueSetter<bool> guard(ignoreCallbacks,true);
              button.setToggleState(value>=.5f,juce::sendNotificationSync);
              resetReference();
          },nullptr) {
        input.setRange(0,1); output.setRange(0,1);
        input.addListener(this); output.addListener(this); button.addListener(this);
        inputAttachment.sendInitialUpdate(); outputAttachment.sendInitialUpdate(); linkAttachment.sendInitialUpdate();
    }
    ~GainLinkAttachment() override {
        if(dragging==&input) inputAttachment.endGesture();
        if(outputGesture || dragging==&output) outputAttachment.endGesture();
        input.removeListener(this); output.removeListener(this); button.removeListener(this);
    }
private:
    bool linked() const { return linkValue.getValue()>=.5f; }
    void resetReference() { reference.reset(inputValue.getValue(),outputValue.getValue()); }
    void updateSlider(juce::Slider& slider,float value) {
        juce::ScopedValueSetter<bool> guard(ignoreCallbacks,true);
        slider.setValue(value,juce::sendNotificationSync);
        if(!updatingPair) resetReference();
    }
    void buttonClicked(juce::Button*) override {
        if(ignoreCallbacks) return;
        linkAttachment.setValueAsCompleteGesture(button.getToggleState()?1.f:0.f);
    }
    void sliderValueChanged(juce::Slider* slider) override {
        if(ignoreCallbacks) return;
        if(slider==&input) {
            const float value=static_cast<float>(input.getValue());
            const bool compensate=linked();
            const float compensated=compensate?reference.outputFor(value):0.f;
            juce::ScopedValueSetter<bool> guard(updatingPair,true);
            if(dragging==&input) inputAttachment.setValueAsPartOfGesture(value);
            else inputAttachment.setValueAsCompleteGesture(value);
            if(compensate) {
                if(dragging==&input && !outputGesture) { outputAttachment.beginGesture(); outputGesture=true; }
                if(outputGesture) outputAttachment.setValueAsPartOfGesture(compensated);
                else outputAttachment.setValueAsCompleteGesture(compensated);
            } else resetReference();
        } else {
            if(dragging==&output) outputAttachment.setValueAsPartOfGesture(static_cast<float>(output.getValue()));
            else outputAttachment.setValueAsCompleteGesture(static_cast<float>(output.getValue()));
            resetReference();
        }
    }
    void sliderDragStarted(juce::Slider* slider) override {
        dragging=slider;
        if(slider==&input) {
            inputAttachment.beginGesture();
            if(linked()) { outputAttachment.beginGesture(); outputGesture=true; }
        } else outputAttachment.beginGesture();
    }
    void sliderDragEnded(juce::Slider* slider) override {
        if(slider==&input) {
            inputAttachment.endGesture();
            if(outputGesture) { outputAttachment.endGesture(); outputGesture=false; }
        } else outputAttachment.endGesture();
        dragging=nullptr;
    }
    juce::RangedAudioParameter& inputValue;
    juce::RangedAudioParameter& outputValue;
    juce::RangedAudioParameter& linkValue;
    juce::Slider& input;
    juce::Slider& output;
    juce::Button& button;
    juce::ParameterAttachment inputAttachment,outputAttachment,linkAttachment;
    InputOutputLink reference;
    juce::Slider* dragging=nullptr;
    bool ignoreCallbacks=false,updatingPair=false,outputGesture=false;
};
