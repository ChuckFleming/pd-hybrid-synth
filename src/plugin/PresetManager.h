#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Simple file-based preset library. Presets are the full APVTS state written as
    XML (`.pdhp`) under the user's application-data folder. On first run a small
    set of factory presets is created so the browser is never empty.
*/
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    void savePreset (const juce::String& name);
    void loadPreset (const juce::String& name);
    void deletePreset (const juce::String& name);        // remove the named preset file
    void loadByOffset (int delta);                       // browse prev/next, wrapping

    juce::StringArray getPresetNames() const;            // sorted, no extension
    juce::String      getCurrentPresetName() const { return currentName_; }

    static juce::File presetDirectory();

private:
    void createFactoryPresetsIfNeeded();
    void setParam (juce::StringRef paramId, float value);

    juce::AudioProcessorValueTreeState& apvts_;
    juce::String currentName_;

    static constexpr const char* kExt = ".pdhp";
};
