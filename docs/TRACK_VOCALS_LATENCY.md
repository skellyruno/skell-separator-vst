# Zero-added-latency vocal tracking monitor (research)

Use **header menu → LIVE TRACK MONITOR** before recording your vocals. This is
transparent dry input monitoring: the plugin reports **0 samples** of PDC and
passes each incoming mic sample to the output without a separator delay.
Nonfinite input is muted; ordinary floating-point levels, stereo polarity and
transients are preserved exactly. The default remains Mix, with the preexisting
2880-sample fixed delay in the neural-enabled plugin.

**This is NOT low-latency source separation.** Ambience/Voice/Reverb stem controls
are inactive for the monitored audio. Neural weights and inference are not used
while tracking. For a live vocal with other effects, route the clean microphone
through Skell Preset's zero-lookahead effects, then separate imported/recorded
mixtures later in Mix/AI mode. The interface, driver, DAW buffer and other chain
plugins still contribute round-trip monitoring latency.

Changing this mode changes reported plugin delay compensation (0 ↔ 2880). Stop
playback/recording before switching; not every host realigns active recording
tracks glitch-free after a dynamic PDC update. FL Studio host tests remain due.
Existing `engineMode`, quality and stem automation IDs are unchanged. The
`trackingMode` setting is stored separately in project state and is **not** a
realtime-automatable parameter, precisely because of host PDC notification.

The 44.1k StemgenRT research graph is capable of a shorter hop, but this branch
currently wraps it in the original 480-sample Skell transport at 2880-sample PDC.
Lower-latency AI-separated monitoring will require a separate 128-hop scheduling
path and Windows/FL Studio deadline and quality checks, not just reducing PDC.
