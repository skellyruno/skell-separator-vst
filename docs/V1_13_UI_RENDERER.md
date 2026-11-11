# Skell Separator / real-editor screenshot validation

Run the `Skell Separator / Actual JUCE UI screenshots` GitHub Actions workflow after source changes (or dispatch manually). The optional CMake target creates the *real* `SkellSeparatorAudioProcessor` and `SkellSeparatorAudioProcessorEditor` under Xvfb on Linux and calls SkellCore's pinned `HeadlessUIRenderer` from the shared template at commit `568a17fd`.

Scenes: compact default (785 x 448) Classical, Bypass, Voice Solo and Neural; standard 1047 x 597; large 1571 x 896. The workflow expects six valid nonblank PNGs and a JSON manifest, and checks child bounds and that the compact modes produce distinct images. It uploads `Skell-Separator-Real-UI-Screenshots` as a separate artifact from the Windows VST3 build.

UI capture uses no fake UI and has no effect on normal production builds (`SKELL_BUILD_UI_RENDERER` is OFF by default). Screenshot checks cover paint/layout at the supplied sizes and basic APVTS mode propagation. They do **not** prove mouse hit-testing, FL Studio DPI handling, audio accuracy, or visual approval. Inspect the PNG artifact before considering the UI finished.
