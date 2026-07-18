#include "PresetManager.h"
#include <initializer_list>
#include <map>
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

namespace {
// Relative path of a preset file (below the preset root), '/'-separated and with
// the extension stripped: e.g. "Scanned/Living Pad".
juce::String relativePath (const juce::File& file, const juce::File& root)
{
    auto rel = file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    if (rel.endsWithIgnoreCase (".pdhp"))
        rel = rel.dropLastCharacters (5);
    return rel;
}
}

void PresetManager::savePreset (const juce::String& path)
{
    const auto trimmed = path.trim();
    if (trimmed.isEmpty())
        return;

    auto file = presetDirectory().getChildFile (trimmed + kExt);
    file.getParentDirectory().createDirectory();

    if (auto state = apvts_.copyState(); state.isValid())
        if (auto xml = state.createXml())
        {
            xml->writeTo (file);
            currentName_ = trimmed;
        }
}

void PresetManager::loadPreset (const juce::String& path)
{
    auto file = presetDirectory().getChildFile (path + kExt);
    if (! file.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid())
        {
            apvts_.replaceState (tree);
            currentName_ = path;
        }
    }
}

void PresetManager::deletePreset (const juce::String& path)
{
    auto file = presetDirectory().getChildFile (path + kExt);
    if (file.existsAsFile())
        file.deleteFile();
    if (currentName_ == path)
        currentName_ = {};
}

void PresetManager::loadByOffset (int delta)
{
    const auto paths = getPresetPaths();
    if (paths.isEmpty())
        return;

    int index = paths.indexOf (currentName_);
    if (index < 0)
        index = 0;

    index = (index + delta) % paths.size();
    if (index < 0)
        index += paths.size();

    loadPreset (paths[index]);
}

PresetManager::Tree PresetManager::getPresetTree() const
{
    Tree tree;
    std::map<juce::String, juce::StringArray> folderMap;   // sorted by folder name

    const auto dir = presetDirectory();
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, true,
                                             juce::String ("*") + kExt))
    {
        const auto rel = relativePath (f, dir);
        const int slash = rel.indexOfChar ('/');
        if (slash < 0)
            tree.root.add (rel);
        else
            folderMap[rel.substring (0, slash)].add (rel.substring (slash + 1));
    }

    tree.root.sortNatural();
    for (auto& [name, presets] : folderMap)
    {
        presets.sortNatural();
        tree.folders.push_back ({ name, presets });
    }
    return tree;
}

juce::StringArray PresetManager::getPresetPaths() const
{
    const auto tree = getPresetTree();
    juce::StringArray paths = tree.root;
    for (const auto& folder : tree.folders)
        for (const auto& p : folder.presets)
            paths.add (folder.name + "/" + p);
    return paths;
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

    // Create any factory preset whose file is missing. Adds newly-shipped patches
    // on upgrade without touching the user's own saved presets.
    auto ensure = [&] (const juce::String& path,
                       std::initializer_list<std::pair<const char*, double>> values)
    {
        if (dir.getChildFile (path + kExt).existsAsFile())
            return;
        apvts_.replaceState (defaults);
        for (const auto& v : values)
            setParam (v.first, static_cast<float> (v.second));
        savePreset (path);
    };

    ensure ("Init", {});

    // --- Phase Distortion (the Casio CZ engine) ---
    ensure ("Phase Distortion/CZ Brass", { {"oscAType",0}, {"oscAWave",0}, {"oscAAmount",0.7f},
        {"cutoff",6000}, {"filterEnvAmount",1.5f}, {"attack",0.02f}, {"decay",0.3f},
        {"sustain",0.7f}, {"release",0.3f} });
    ensure ("Phase Distortion/Glass Bells", { {"oscAType",0}, {"oscAWave",3}, {"oscAAmount",0.6f},
        {"cutoff",9000}, {"attack",0.002f}, {"decay",1.2f}, {"sustain",0}, {"release",1.2f},
        {"delayMix",0.3f}, {"delayFeedback",0.4f} });
    ensure ("Phase Distortion/Resonant Sweep", { {"oscAType",0}, {"oscAWave",5}, {"oscAAmount",0.5f},
        {"cutoff",2000}, {"filterEnvAmount",2.0f}, {"attack",0.01f}, {"decay",0.5f}, {"sustain",0.4f} });

    // --- Vector Phaseshaping ---
    ensure ("Vector PS/Formant Key", { {"oscAType",5}, {"oscAAmount",0.5f}, {"oscAPulseWidth",0.5f},
        {"cutoff",8000}, {"sustain",0.8f}, {"decay",0.4f} });
    ensure ("Vector PS/Sync Lead", { {"oscAType",5}, {"oscAAmount",0.9f}, {"oscAPulseWidth",0.35f},
        {"cutoff",7000}, {"glideMode",2}, {"glideTime",0.05f}, {"unisonVoices",2}, {"drive",3.0f} });
    ensure ("Vector PS/Vowel Sweep", { {"oscAType",5}, {"oscAAmount",0.3f}, {"dcwEnvAmount",0.6f},
        {"cutoff",6000}, {"attack",0.05f}, {"release",1.0f}, {"sustain",0.7f} });

    // --- Scanned synthesis ---
    ensure ("Scanned/Living Pad", { {"oscAType",6}, {"oscAAmount",0.5f}, {"oscAPulseWidth",0.1f},
        {"attack",0.5f}, {"release",1.5f}, {"sustain",0.9f}, {"unisonVoices",2}, {"cutoff",7000} });
    ensure ("Scanned/Scan Voice", { {"oscAType",6}, {"oscAExcite",2}, {"oscAAmount",0.6f},
        {"oscAPulseWidth",0.2f}, {"cutoff",6000}, {"sustain",0.8f} });
    ensure ("Scanned/Metal Scan", { {"oscAType",6}, {"oscAExcite",1}, {"oscAAmount",0.8f},
        {"oscAPulseWidth",0.3f}, {"cutoff",8000}, {"attack",0.002f}, {"decay",0.6f}, {"sustain",0.4f} });

    // --- VOSIM (vocal formant) ---
    ensure ("VOSIM/Robot Vox", { {"oscAType",7}, {"oscAAmount",0.4f}, {"oscAPulseWidth",0.7f},
        {"oscAEngine",0.43f}, {"cutoff",8000}, {"sustain",0.8f} });
    ensure ("VOSIM/Vowel Pad", { {"oscAType",7}, {"oscAAmount",0.3f}, {"oscAPulseWidth",0.8f},
        {"attack",0.4f}, {"release",1.2f}, {"sustain",0.85f}, {"unisonVoices",3}, {"unisonDetune",12} });
    ensure ("VOSIM/Buzz Lead", { {"oscAType",7}, {"oscAAmount",0.6f}, {"oscAEngine",0.85f},
        {"glideMode",2}, {"glideTime",0.05f}, {"drive",4.0f}, {"cutoff",7000} });

    // --- Walsh (digital / chiptune) ---
    ensure ("Walsh/Chip Lead", { {"oscAType",8}, {"oscAAmount",0.8f}, {"oscAPulseWidth",0.5f},
        {"cutoff",9000}, {"attack",0.002f}, {"decay",0.2f}, {"sustain",0.5f}, {"glideMode",2} });
    ensure ("Walsh/Digital Bass", { {"oscAType",8}, {"oscAAmount",0.4f}, {"oscAOctave",-1},
        {"cutoff",1500}, {"filterEnvAmount",1.5f}, {"attack",0.002f}, {"sustain",0.7f} });
    ensure ("Walsh/Fold Grit", { {"oscAType",8}, {"oscAAmount",0.5f}, {"oscAEngine",0.9f},
        {"drive",3.0f}, {"cutoff",7000}, {"sustain",0.7f} });

    // --- Pluck (Karplus-Strong) ---
    ensure ("Pluck/PD Pluck", { {"oscAType",0}, {"pluckOn",1}, {"pluckDecay",0.85f}, {"pluckDamp",0.3f},
        {"cutoff",8000}, {"attack",0.002f}, {"decay",0.1f}, {"sustain",0.7f}, {"release",1.0f} });
    ensure ("Pluck/Koto", { {"oscAType",0}, {"pluckOn",1}, {"pluckDecay",0.9f}, {"pluckDamp",0.2f},
        {"pluckDispersion",0.4f}, {"sustain",0.6f}, {"release",1.2f} });
    ensure ("Pluck/Mallet", { {"oscAType",1}, {"pluckOn",1}, {"pluckDecay",0.6f}, {"pluckDamp",0.5f},
        {"pluckBurst",5.0f}, {"attack",0.002f}, {"decay",0.2f}, {"sustain",0.5f} });

    // --- Analog (subtractive) ---
    ensure ("Analog/Fat Saw", { {"oscAType",1}, {"unisonVoices",6}, {"unisonDetune",28},
        {"cutoff",4000}, {"resonance",0.35f} });
    ensure ("Analog/Square Lead", { {"oscAType",2}, {"oscAPulseWidth",0.35f}, {"cutoff",6000},
        {"glideMode",2}, {"glideTime",0.05f}, {"drive",3.0f}, {"sustain",0.8f} });
    ensure ("Analog/Warm Pad", { {"oscAType",1}, {"cutoff",2500}, {"attack",0.8f}, {"release",1.5f},
        {"sustain",0.8f}, {"unisonVoices",4}, {"unisonDetune",18}, {"delayMix",0.25f},
        {"delayFeedback",0.35f}, {"filterEnvAmount",1.0f} });

    // --- Bass ---
    ensure ("Bass/Sub Bass", { {"bassOn",1}, {"bassLevel",0.9f}, {"bassOctave",-1},
        {"cutoff",1200}, {"filterEnvAmount",2.0f} });
    ensure ("Bass/Acid Bass", { {"oscAType",1}, {"cutoff",1000}, {"resonance",0.8f},
        {"filterEnvAmount",2.0f}, {"glideMode",1}, {"glideTime",0.08f}, {"drive",6.0f} });
    ensure ("Bass/Deep Bass", { {"bassOn",1}, {"bassLevel",0.9f}, {"bassOctave",-1}, {"oscAType",1},
        {"oscAOctave",-1}, {"cutoff",900}, {"attack",0.005f}, {"release",0.3f}, {"sustain",0.9f} });

    apvts_.replaceState (defaults);
    currentName_ = {};   // show the placeholder; live state is the plugin default
}
