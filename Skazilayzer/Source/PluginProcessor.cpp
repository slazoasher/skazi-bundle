#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../Common/UpdateNotice.h"

juce::AudioProcessorEditor* SkzlProcessor::createEditor() { return skazi::withUpdateNotice (new SkzlEditor (*this)); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkzlProcessor(); }
