// Offline layout check: does every tab page fit its window without scrolling?
//
// That requirement is the whole point of the 1280 x 800 rebuild, and until now
// nothing enforced it. Each time the layout moved I added a temporary
// diagnostic, read the numbers and deleted it -- which verifies the code on the
// afternoon it was written and nothing after. A card that grows past the window
// later would simply start scrolling, quietly.
//
// This builds a real processor and editor headlessly and asks each page for the
// height it needs. Non-zero exit on any overflow, so it can gate a build.
//
// Run: pdhybrid_uicheck [width height]

#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <memory>

namespace {

// The size the editor opens at. Kept here rather than read from the editor so a
// change to the default is a deliberate edit in two places, not a silent one
// that also moves the goalposts this check measures against.
constexpr int kDefaultW = 1280;
constexpr int kDefaultH = 800;

// The minimum the window can be dragged to (setResizeLimits). Pages are allowed
// to need scrolling here -- that is what the Viewport is for -- so this run is
// reported but not enforced.
constexpr int kMinW = 1100;
constexpr int kMinH = 700;

int runAt (PDHybridEditor& editor, int w, int h, bool enforce)
{
    std::printf ("\n%d x %d%s\n", w, h, enforce ? "" : "   (minimum size, scrolling allowed)");

    int failures = 0;
    for (const auto& f : editor.measurePageFit (w, h))
    {
        const bool ok = f.fits();
        std::printf ("  %-7s needs %4d of %4d   slack %5d  %s\n",
                     f.name.toRawUTF8(), f.needed, f.available,
                     f.available - f.needed,
                     ok ? "ok" : (enforce ? "OVERFLOW" : "scrolls"));
        if (enforce && ! ok)
            ++failures;
    }
    return failures;
}

} // namespace

int main (int argc, char** argv)
{
    // A GUI context is needed to build components, but no window is shown.
    juce::ScopedJuceInitialiser_GUI juceInit;

    int w = kDefaultW, h = kDefaultH;
    if (argc >= 3)
    {
        w = std::atoi (argv[1]);
        h = std::atoi (argv[2]);
    }

    PDHybridAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // The editor builds every page in its constructor, which is what we measure.
    std::unique_ptr<PDHybridEditor> editor (
        dynamic_cast<PDHybridEditor*> (proc.createEditor()));

    if (editor == nullptr)
    {
        std::printf ("could not create the editor\n");
        return 2;
    }

    std::printf ("PD Hybrid layout check");

    int failures = runAt (*editor, w, h, true);
    if (w == kDefaultW && h == kDefaultH)
        runAt (*editor, kMinW, kMinH, false);

    std::printf ("\n%s\n\n", failures == 0
                 ? "every page fits"
                 : "AT LEAST ONE PAGE OVERFLOWS -- it will scroll, which the layout"
                   " is meant to make unnecessary");
    return failures == 0 ? 0 : 1;
}
