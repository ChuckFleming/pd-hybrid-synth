#include "PresetManager.h"
#include <initializer_list>
#include <utility>

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

    // Snapshot the defaults so each factory patch starts from a clean slate.
    const auto defaults = apvts_.copyState();

    // Create any factory preset whose file is missing. This adds newly-shipped
    // factory patches on upgrade without touching the user's own saved presets.
    auto ensure = [&] (const juce::String& name,
                       std::initializer_list<std::pair<const char*, float>> values)
    {
        if (dir.getChildFile (name + kExt).existsAsFile())
            return;
        apvts_.replaceState (defaults);
        for (const auto& v : values)
            setParam (v.first, v.second);
        savePreset (name);
    };

    ensure ("Init", {});
    ensure ("Fat Saw", { {"oscAType",1.0f}, {"unisonVoices",6.0f}, {"unisonDetune",28.0f},
                         {"cutoff",4000.0f}, {"resonance",0.35f} });
    ensure ("Sub Bass", { {"bassOn",1.0f}, {"bassLevel",0.9f}, {"bassOctave",-1.0f},
                          {"cutoff",1200.0f}, {"filterEnvAmount",2.0f} });

    // --- 10 additional library patches ---
    ensure ("Warm Pad", { {"oscAType",0.0f}, {"oscAAmount",0.5f}, {"cutoff",2500.0f},
                          {"attack",0.8f}, {"release",1.5f}, {"sustain",0.8f},
                          {"unisonVoices",4.0f}, {"unisonDetune",18.0f},
                          {"delayMix",0.25f}, {"delayFeedback",0.35f}, {"filterEnvAmount",1.0f} });
    ensure ("Pluck Keys", { {"oscAType",0.0f}, {"cutoff",6000.0f}, {"attack",0.002f},
                            {"decay",0.25f}, {"sustain",0.0f}, {"release",0.2f},
                            {"filterEnvAmount",2.5f}, {"resonance",0.3f}, {"driveOn",1.0f}, {"drive",4.0f} });
    ensure ("Acid Lead", { {"oscAType",1.0f}, {"filterType",0.0f}, {"cutoff",1200.0f},
                           {"resonance",0.8f}, {"filterEnvAmount",2.0f}, {"glideMode",1.0f},
                           {"glideTime",0.08f}, {"mod1Source",2.0f}, {"mod1Dest",4.0f},
                           {"mod1Depth",0.35f}, {"lfoRate",6.0f}, {"drive",6.0f} });
    ensure ("Deep Bass", { {"bassOn",1.0f}, {"bassLevel",0.9f}, {"bassOctave",-1.0f},
                           {"bassHarmonics",0.3f}, {"oscALevel",0.5f}, {"cutoff",900.0f},
                           {"attack",0.005f}, {"release",0.3f}, {"sustain",0.9f} });
    ensure ("Unison Stab", { {"oscAType",1.0f}, {"unisonVoices",6.0f}, {"unisonDetune",30.0f},
                             {"unisonWidth",0.8f}, {"cutoff",5000.0f}, {"attack",0.002f},
                             {"decay",0.4f}, {"sustain",0.2f}, {"drive",5.0f}, {"resonance",0.25f} });
    ensure ("Dream Bells", { {"oscAType",0.0f}, {"oscAWave",5.0f}, {"oscAAmount",0.6f},
                             {"cutoff",8000.0f}, {"attack",0.002f}, {"decay",1.2f}, {"sustain",0.0f},
                             {"release",1.2f}, {"delayMix",0.3f}, {"delayFeedback",0.4f}, {"filterEnvAmount",1.5f} });
    ensure ("Wobble Bass", { {"oscAType",1.0f}, {"cutoff",800.0f}, {"resonance",0.6f}, {"filterType",0.0f},
                             {"mod1Source",9.0f}, {"mod1Dest",4.0f}, {"mod1Depth",0.5f}, {"lfo2Rate",3.0f},
                             {"drive",8.0f}, {"oscBLevel",0.5f}, {"oscBType",2.0f}, {"oscBOctave",-1.0f} });
    ensure ("Glass Pad", { {"oscAType",0.0f}, {"oscAWave",3.0f}, {"cutoff",9000.0f}, {"attack",1.0f},
                           {"release",2.0f}, {"sustain",0.7f}, {"oscAEqHigh",6.0f}, {"panSpread",0.5f},
                           {"unisonVoices",3.0f}, {"unisonDetune",12.0f}, {"delayMix",0.2f} });
    ensure ("Hard Lead", { {"oscAType",1.0f}, {"driveOn",1.0f}, {"driveType",2.0f}, {"drive",12.0f},
                           {"cutoff",7000.0f}, {"resonance",0.3f}, {"attack",0.003f}, {"sustain",0.8f},
                           {"glideMode",2.0f}, {"glideTime",0.05f}, {"unisonVoices",2.0f}, {"unisonDetune",10.0f} });
    ensure ("Drift Strings", { {"oscAType",1.0f}, {"unisonVoices",5.0f}, {"unisonDetune",20.0f}, {"drift",0.6f},
                               {"cutoff",3500.0f}, {"attack",0.4f}, {"release",1.0f}, {"sustain",0.85f},
                               {"filterEnvAmount",0.8f}, {"delayMix",0.2f} });

    apvts_.replaceState (defaults);
    currentName_ = {};   // show the placeholder; live state is the plugin default
}
