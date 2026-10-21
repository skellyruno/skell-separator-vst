# Host smoke test plan — FL Studio / JUCE VST3

These are manual tests to run on a **fresh project**, not claims of completed
validation. Keep an existing project backed up when evaluating the plugin.

1. In GitHub Actions check that both **Build Skell Separator** and **DSP
   Regression Tests** are green. Extract the Windows artifact and install the
   `Skell Separator.vst3` bundle (do not merely copy the bundled `.dll`).
2. Open a fresh FL Studio project, load and unload Skell Separator 10 times,
   then close and reopen the project. Verify that Fruity Wrapper never raises
   the "crashed while loading its settings" message.
3. Save a project after changing all 3 stem gains, Solo/Mute, channelMode,
   bypass and quality. Restart FL Studio and confirm parameter state restoration.
4. Automate each knob at fast rates. Change Mono/Stereo and bypass repeatedly
   with a 1 kHz test tone and recorded stereo drums. Listen for clicks.
5. Render a known source at unity stem gains and no Solo/Mute; compare against
   a dry copy aligned by 1024 samples. Mismatch should be close to float noise
   away from extreme floating-point summations.
6. Check **both** the Skell header Bypass button **and the host-native bypass** (`processBlockBypassed`) using an impulse at the plugin input.
   The new override shares the 1024-sample processing timeline and crossfade; verify
   the DAW actually calls it, since some hosts bypass at the wrapper level instead.
   Check bypass: the output should equal dry stereo delayed by 1024 samples,
   not zero-latency dry. In Mono mode, the bypass should still preserve the
   original L/R channels. Ensure host plugin delay compensation is enabled.
7. Run 32/64/256/512/1024 sample buffer sizes, 44.1/48/96 kHz, mono/stereo
   track layouts, silence, brief bursts and transport play/stop loops.
8. Monitor CPU usage and artifacts using actual sung and spoken vocals, and
   dense music. Rate voice/reverb quality subjectively; current fallback is
   not claimed to meet modern neural source-separation quality.
9. Re-test after any JUCE update or change to FFT, inference, renderer lifecycle
   or plugin state logic.

10. Feed a stereo L = -R sinusoid and a hard-left vocal recording in **Stereo**
    mode. Solo Voice and confirm it remains audible (v1.2 could reject an
    anti-phase source completely). This proves a regression only, not studio-grade
    source separation.
11. If Windows CI fails, attach the **first compiler diagnostic** as well as the
    version of JUCE CMake downloaded. Linux core-only DSP tests deliberately do
    not build the graphical editor, VST3 wrapper, or JUCE platform backends.
