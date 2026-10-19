#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
class DrummerEditor final: public juce::AudioProcessorEditor,private juce::Timer {
public:
 explicit DrummerEditor(DrummerProcessor&);~DrummerEditor()override;
 void resized()override;
 void paint(juce::Graphics&)override;
private:
 DrummerProcessor& plugin;
 bool pageReady=false;
 int monitoredKey=48;
 juce::String sentPattern;
 juce::WebBrowserComponent browser;
 struct ResizeBorder:juce::ResizableBorderComponent {
  using juce::ResizableBorderComponent::ResizableBorderComponent;
  void paint(juce::Graphics&)override{};
 } resizeBorder{this,getConstrainer()};
 struct ResizeCorner:juce::ResizableCornerComponent {
  using juce::ResizableCornerComponent::ResizableCornerComponent;
  bool hitTest(int,int)override{return true;}
  void paint(juce::Graphics& g)override{
   g.setColour(findColour(juce::ResizableWindow::backgroundColourId).contrasting(.45f));
   for(float inset:{7.f,13.f,19.f})g.drawLine(getWidth()-inset,getHeight()-5.f,getWidth()-5.f,getHeight()-inset,1.5f);
  }
 } resizeCorner{this,getConstrainer()};
 juce::Colour background{0xff0a0c10};
 std::unique_ptr<juce::FileChooser> chooser;
 std::optional<juce::WebBrowserComponent::Resource> provide(const juce::String&);
 void timerCallback()override;
 using Complete=juce::WebBrowserComponent::NativeFunctionCompletion;
 void request(const juce::Array<juce::var>&,Complete);
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrummerEditor)
};
