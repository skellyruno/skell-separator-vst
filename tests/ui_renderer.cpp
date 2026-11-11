// Captures the *actual* JUCE processor/editor using SkellCore's shared
// screenshot harness. No fake design components or manually drawn mockups.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <SkellCore/HeadlessUIRenderer.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>

static void setParameter(juce::AudioProcessorValueTreeState& state,
                         const juce::String& name, float value) {
    if (auto* p = state.getParameter(name)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    auto output = juce::File::getCurrentWorkingDirectory().getChildFile(
        argc >= 3 && juce::String(argv[1]) == "--output"
            ? juce::String::fromUTF8(argv[2]) : "ui-screenshots");
    SkellSeparatorAudioProcessor processor;
    processor.prepareToPlay(48000., 256);
    int failures = 0;
    {
        SkellSeparatorAudioProcessorEditor editor(processor);
        skellcore::ui::headless::Options options;
        options.project = "Skell Separator actual JUCE PluginEditor 1.13";
        options.minimumColours = 12;
        options.scenes = {
            {"compact-classical", 785, 448, 0, 0, false},
            {"compact-bypass", 785, 448, 0, 1, false},
            {"compact-voice-solo", 785, 448, 0, 2, false},
            {"compact-neural", 785, 448, 0, 3, false},
            {"standard-classical", 1047, 597, 0, 0, false},
            {"large-classical", 1571, 896, 0, 0, false}
        };
        options.configure = [&](const skellcore::ui::headless::Scene& scene) {
            processor.usePreset(0);
            setParameter(processor.apvts, "bypass", scene.mode == 1 ? 1.0f : 0.0f);
            setParameter(processor.apvts, "voiceSolo", scene.mode == 2 ? 1.0f : 0.0f);
            setParameter(processor.apvts, "engineMode", scene.mode == 3 ? 1.0f : 0.0f);
            // Update the actual editor state synchronously on the JUCE GUI
            // thread. Modal message-pumping APIs are unavailable to plugins.
            editor.refreshForScreenshot();
        };
        options.inspect = [&] {
            juce::StringArray errors;
            const auto box = editor.getLocalBounds();
            int headers = 0, stemColumns = 0;
            for (int i = 0; i < editor.getNumChildComponents(); ++i) {
                auto* child = editor.getChildComponent(i);
                headers += dynamic_cast<HeaderBar*>(child) != nullptr;
                stemColumns += dynamic_cast<StemColumn*>(child) != nullptr;
                // JUCE itself adds a resize-corner child for setResizable(true,true).
                if (child->isVisible() && !box.contains(child->getBounds()))
                    errors.add("child outside editor canvas");
                if (child->getWidth() <= 0 || child->getHeight() <= 0)
                    errors.add("zero-sized UI child");
            }
            if (headers != 1 || stemColumns != 3)
                errors.add("expected one real header and exactly three real stem panels");
            return errors;
        };
        options.mustDiffer = {
            {"compact-classical", "compact-bypass"},
            {"compact-classical", "compact-voice-solo"},
            {"compact-classical", "compact-neural"}
        };
        const auto result = skellcore::ui::headless::render(editor, output, options);
        for (const auto& error : result.errors) {
            std::fprintf(stderr, "Skell Separator UI: %s\n", error.toRawUTF8());
            ++failures;
        }
        if (result.captured != static_cast<int>(options.scenes.size()))
            ++failures;
        std::printf("Skell Separator actual-editor screenshots: %d, failures: %d\n",
                    result.captured, failures);
    }
    processor.releaseResources();
    return failures == 0 ? 0 : 1;
}
