#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

/**
    File-based preset library with one level of category folders. Presets are the
    full APVTS state written as XML (`.pdhp`) under the user's application-data
    folder; factory patches live in category subfolders (Presets/<Category>/) and
    are created on first run so the browser is never empty. User "Save" writes to
    the top level. A preset is addressed by its relative path, e.g. "Scanned/Pad"
    or (top level) "My Patch".
*/
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    void savePreset  (const juce::String& path);
    void loadPreset  (const juce::String& path);
    void deletePreset (const juce::String& path);
    void loadByOffset (int delta);                       // browse prev/next, wrapping

    // Hierarchical view for the browser menu: top-level presets plus one level of
    // category folders, each with its preset names (leaf names, no folder prefix).
    struct Folder { juce::String name; juce::StringArray presets; };
    struct Tree   { juce::StringArray root; std::vector<Folder> folders; };
    Tree getPresetTree() const;

    // Flat, ordered list of relative paths (top level first, then folders) used
    // for prev/next stepping.
    juce::StringArray getPresetPaths() const;

    juce::String getCurrentPresetName() const { return currentName_; }

    static juce::File presetDirectory();

private:
    void createFactoryPresetsIfNeeded();
    void setParam (juce::StringRef paramId, float value);

    juce::AudioProcessorValueTreeState& apvts_;
    juce::String currentName_;   // relative path of the current preset

    static constexpr const char* kExt = ".pdhp";
};
