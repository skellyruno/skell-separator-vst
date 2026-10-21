# Skell VST3 Quickstart Template

Reusable JUCE 8 / CMake / GitHub Actions Windows VST3 starter derived from the **build structure** of your uploaded skell-key-vst project. Unlike the TONIC-specific project, this is a generic **gain effect** with a working parameter, GUI knob, preset-state serialization, mono/stereo support, and automated GitHub packaging.

## Fast start

1. Create a new GitHub repository and upload the **contents** of this folder (including `.github`).
2. Open **Actions → Build Windows VST3 → Run workflow**.
3. After a successful build, download the workflow artifact containing the `.vst3` bundle.
4. Or run `python tools/new_plugin.py "Skell Compressor" ../skell-compressor --code Sk03` to generate a separate renamed project. Use a **different 4-character plugin code** for every released plugin; the manufacturer code should remain stable for your brand.

## Local build

`cmake -S . -B build -A x64 -DJUCE_PATH=C:/JUCE`

`cmake --build build --config Release --parallel 2`

Without `JUCE_PATH`, CMake fetches JUCE 8.0.12 from GitHub.

## Where to edit

- `Source/PluginProcessor.cpp`: parameters and DSP (the sample multiplies incoming audio by Gain).
- `Source/PluginEditor.cpp`: GUI and knob layout.
- `CMakeLists.txt`: company, unique plugin code, bundle ID, plugin format, name.
- `.github/workflows/build.yml`: CI compilation and artifact packaging.

## Important

This is **not** a synthesizer template; it is an audio effect. It does not include TONIC's pitch/key detector or third-party graphical assets. The supplied original ZIP was used to guide project layout and the GitHub workflow. No original TONIC code is bundled. A successful GitHub Actions build has not been verified in this environment.
