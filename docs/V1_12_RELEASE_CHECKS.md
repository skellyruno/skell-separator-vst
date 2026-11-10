# v1.12 verification and remaining gaps

## Code-level decisions

1. Expired neural frames are skipped when the producer has moved >= 2,400 samples beyond a queued frame's start; 480 samples of model look-behind leaves the 2,880-sample output contract. The model history is re-primed after any gap. This makes no claims about measured improvement in trained-model quality, and under overload a fallback may be used more often.
2. Add an OVER 0 dBFS post-mix **sample-peak** warning, not a limiter or intersample true-peak detector. DAW floating-point audio may legitimately exceed full scale internally. The plugin does not impose output limiting.
3. Keep idle spectrum meters from repainting once settled. Preserve editor size changes in saved APVTS state. Solo/Mute buttons stay inside each stem panel.
4. Use `skell::neuralContractLatency` as the sole fixed model delay source for alignment, preserving the previous reported sample latency and automation IDs.

## Tests to run

```sh
bash tests/run_dsp_tests.sh
bash tests/run_neural_contract.sh
bash tests/run_neural_blend.sh
bash tests/run_tiny_vocal.sh
bash tests/run_tiny_benchmark.sh
python tests/ui_layout_contract.py
python tests/offline_tiny_render_smoke.py
python tests/real_vocal_quality.py --self-test
bash tests/run_cpu_profile.sh 48000 256 8
```

Use `SKELL_SANITIZE=1` with the C++ test scripts and GCC/Clang for memory and UB checks. GitHub Actions must compile and package the JUCE Windows VST3. The static UI geometry test cannot prove rendering, gesture behavior or performance in FL Studio.

## Limitations

- TinyVocalNet is trained on procedural mixtures only. It still trails the classical estimate on earlier recorded spoken-voice checks, and hasn't been benchmarked using real isolated singing references.
- Reverb is classical/heuristic, not an independent trained neural source.
- No FL Studio host loading, closing, automation or DPI screenshot test has been performed here.
- CPU figures from Linux callback-only measurements exclude Windows hosts and inference overhead; avoid interpreting them as universal CPU usage.
