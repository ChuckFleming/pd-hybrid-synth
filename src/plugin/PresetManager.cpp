#include "PresetManager.h"

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts_ (state)
{
    createFactoryPresetsIfNeeded();
}

juce::File PresetManager::presetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("PD Hybrid Synth")
               .getChildFile ("Presets");
}

void PresetManager::savePreset (const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return;

    auto dir = presetDirectory();
    dir.createDirectory();

    if (auto state = apvts_.copyState(); state.isValid())
        if (auto xml = state.createXml())
        {
            auto file = dir.getChildFile (trimmed + kExt);
            xml->writeTo (file);
            currentName_ = trimmed;
        }
}

void PresetManager::loadPreset (const juce::String& name)
{
    auto file = presetDirectory().getChildFile (name + kExt);
    if (! file.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid())
        {
            apvts_.replaceState (tree);
            currentName_ = name;
        }
    }
}

void PresetManager::loadByOffset (int delta)
{
    const auto names = getPresetNames();
    if (names.isEmpty())
        return;

    int index = names.indexOf (currentName_);
    if (index < 0)
        index = 0;

    index = (index + delta) % names.size();
    if (index < 0)
        index += names.size();

    loadPreset (names[index]);
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : presetDirectory().findChildFiles (juce::File::findFiles, false,
                                                           juce::String ("*") + kExt))
        names.add (f.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

void PresetManager::setParam (juce::StringRef paramId, float value)
{
    if (auto* p = apvts_.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PresetManager::createFactoryPresetsIfNeeded()
{
    auto dir = presetDirectory();
    dir.createDirectory();

    // Presets already exist: leave currentName_ empty so the browser shows its
    // placeholder rather than naming a preset we did not actually load.
    if (dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExt).size() > 0)
        return;

    // Snapshot the defaults so each factory patch starts clean.
    const auto defaults = apvts_.copyState();

    savePreset ("Init");                                   // pure defaults

    setParam ("oscAType", 1.0f);                           // Saw
    setParam ("unisonVoices", 6.0f);
    setParam ("unisonDetune", 28.0f);
    setParam ("cutoff", 4000.0f);
    setParam ("resonance", 0.35f);
    savePreset ("Fat Saw");

    apvts_.replaceState (defaults);
    setParam ("bassOn", 1.0f);
    setParam ("bassLevel", 0.9f);
    setParam ("bassOctave", -1.0f);
    setParam ("cutoff", 1200.0f);
    setParam ("filterEnvAmount", 2.0f);
    savePreset ("Sub Bass");

    apvts_.replaceState (defaults);
    currentName_ = "Init";
}
