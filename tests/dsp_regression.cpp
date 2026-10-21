#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include "dsp/Separator.h"
#include "dsp/StemMixer.h"

namespace {
void maskTests() {
    MaskEstimator mask;
    mask.reset();
    MaskEstimator::Spectrum mid{}, side{}, dry{}, wet{};
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> noise(0.0f,2.0f);
    for(int frame=0;frame<1600;++frame) {
        for(int k=0;k<MaskEstimator::bins;++k) {
            auto i=static_cast<size_t>(k);
            mid[i]= frame<50 ? 0 : 0.3f+noise(gen)+(frame>200&&frame<450?5.0f:0.0f);
            side[i]= noise(gen)*0.1f;
        }
        if(frame==280) mid[42]=std::numeric_limits<float>::infinity();
        mask.process(mid,side,frame%3,dry,wet);
        for(int k=0;k<MaskEstimator::bins;++k) {
            auto i=static_cast<size_t>(k);
            assert(std::isfinite(dry[i])&&std::isfinite(wet[i]));
            assert(dry[i]>=0.0f&&wet[i]>=0.0f);
            assert(dry[i]+wet[i]<=0.97001f);
        }
    }
    mask.reset();
    mid.fill(0.0f); side.fill(0.0f);
    mask.process(mid,side,1,dry,wet);
    for(int k=0;k<MaskEstimator::bins;++k) assert(dry[k]==0.0f&&wet[k]==0.0f);
}

void analyzerResetTests()
{
    SpectralAnalyzer a;
    SpectralAnalyzer::Frame f;
    for (auto& source : f.magnitudes) source.fill(5.0f);
    f.levels.fill(0.9f);
    a.publish(f);
    assert(a.getLevel(0) == 0.9f && a.getBand(2, 47) == 5.0f);
    a.reset();
    for (int s = 0; s < 3; ++s)
    {
        assert(a.getLevel(s) == 0.0f);
        for (int k = 0; k < SpectralAnalyzer::bands; ++k)
            assert(a.getBand(s, k) == 0.0f);
    }
}

void voiceTailAndSilenceTests(int sampleRate)
{
    MaskEstimator m;
    m.prepare(sampleRate);
    MaskEstimator::Spectrum mid{}, side{}, dry{}, wet{};
    mid.fill(0.05f);
    side.fill(0.06f);
    for (int frame = 0; frame < 140; ++frame) m.process(mid, side, 1, dry, wet);
    // A steady, low-energy bed must not be falsely interpreted as reverb.
    assert(wet[95] < 0.01f);
    float maxDry = 0.0f;
    float maxWet = 0.0f;
    for (int frame = 0; frame < 80; ++frame)
    {
        const float voice = frame < 24 ? 10.0f : 10.0f * std::exp(-0.075f * (frame - 24));
        mid.fill(voice + 0.05f);
        side.fill(voice * 0.10f + 0.06f);
        m.process(mid, side, 1, dry, wet);
        maxDry = std::max(maxDry, dry[95]);
        maxWet = std::max(maxWet, wet[95]);
    }
    assert(maxDry > 0.15f);
    assert(maxWet > 0.01f);
    // A long exact silence should reset stale masks and restart cleanly.
    mid.fill(0.0f); side.fill(0.0f);
    for (int frame = 0; frame < static_cast<int>(0.55 * sampleRate / 256.0); ++frame)
    {
        m.process(mid, side, 1, dry, wet);
        for (size_t i = 0; i < dry.size(); ++i)
            assert(std::isfinite(dry[i]) && std::isfinite(wet[i]) && dry[i] >= 0 && wet[i] >= 0 && dry[i]+wet[i]<=0.970001f);
    }
    assert(dry[95] == 0.0f && wet[95] == 0.0f);
    std::cout << "tail tracking at " << sampleRate << " dry=" << maxDry << " wet=" << maxWet << "\n";
}

void blockInvariantTests(int sampleRate, bool mono, int quality) {
    SpectralAnalyzer spectrum;
    Separator separator(spectrum);
    separator.prepare(sampleRate);
    StemMixer mixer;
    mixer.prepare(sampleRate);
    // Expected exactly complementary stems, fixed 1024 sample latency, any
    // 1/17/127/512 block segmentation, mono/stereo mode, varying masks.
    constexpr int sampleCount=23000;
    std::array<float,Separator::latency+sampleCount> left{}, right{};
    float maxError=0, maxDryError=0, maxDifference=0;
    for(int i=0;i<sampleCount;++i) {
        const float a=0.24f*std::sin(2*3.14159265f*201*i/sampleRate)+ (i%191==0?0.9f:0.0f);
        const float b=0.19f*std::sin(2*3.14159265f*443*i/sampleRate)+ (i%271==0?0.6f:0.0f);
        left[static_cast<size_t>(i)]=a;
        right[static_cast<size_t>(i)]=b;
        Separator::Stems stems{};
        std::array<float,2> delayed{};
        separator.processSample(a,b,mono ? 1.0f : 0.0f,quality,stems,delayed);
        const float expectedL=i>=Separator::latency ? (mono ? (left[static_cast<size_t>(i-Separator::latency)]+right[static_cast<size_t>(i-Separator::latency)])*0.5f : left[static_cast<size_t>(i-Separator::latency)]) : 0.0f;
        const float expectedR=i>=Separator::latency ? (mono ? expectedL : right[static_cast<size_t>(i-Separator::latency)]) : 0.0f;
        float sumL=0,sumR=0;
        for(int s=0;s<3;++s) {
            assert(std::isfinite(stems[static_cast<size_t>(s)][0])&&std::isfinite(stems[static_cast<size_t>(s)][1]));
            sumL+=stems[static_cast<size_t>(s)][0];
            sumR+=stems[static_cast<size_t>(s)][1];
        }
        maxError=std::max(maxError,std::max(std::abs(sumL-expectedL),std::abs(sumR-expectedR)));
        const auto mixed=mixer.mix(stems,delayed);
        assert(std::isfinite(mixed[0])&&std::isfinite(mixed[1]));
        maxDryError=std::max(maxDryError,std::abs(delayed[0]-(i>=Separator::latency?left[static_cast<size_t>(i-Separator::latency)]:0)));
        maxDifference=std::max(maxDifference,std::abs(mixed[0]-mixed[1]));
    }
    std::cout<<"sampleRate="<<sampleRate<<" mono="<<mono<<" quality="<<quality
             <<" complement max error="<<maxError<<" dry error="<<maxDryError<<"\n";
    assert(maxError<2.0e-5f);
    assert(maxDryError<1.0e-6f);
    if(mono) assert(maxDifference<1.0e-5f);
}

// Regression for the old mid-only detector: L=-R used to produce exactly
// zero voice stem even for a strong clean tone. The improved detector must
// respond to stereo RMS energy without changing the L/R output phase.
void stereoPolarityTests()
{
    constexpr int sampleRate = 48000;
    const auto run = [](int mode)
    {
        SpectralAnalyzer spectrum;
        Separator separator(spectrum);
        separator.prepare(sampleRate);
        double voiceEnergy = 0.0;
        double inputEnergy = 0.0;
        float maxReconstructionError = 0.0f;
        std::array<float, 22000> inputLeft {}, inputRight {};
        for (int i = 0; i < static_cast<int>(inputLeft.size()); ++i)
        {
            const float sample = i < 2048 ? 0.0005f
                : 0.24f * std::sin(2.0f * 3.14159265359f * 240.0f * i / sampleRate);
            const float left = sample;
            const float right = mode == 0 ? sample : mode == 1 ? -sample : 0.0f;
            inputLeft[static_cast<size_t>(i)] = left;
            inputRight[static_cast<size_t>(i)] = right;
            Separator::Stems stems {};
            std::array<float, 2> dry {};
            separator.processSample(left, right, 0.0f, 2, stems, dry);
            const std::array<float, 2> sum {
                stems[0][0] + stems[1][0] + stems[2][0],
                stems[0][1] + stems[1][1] + stems[2][1] };
            if (i < 3000 || i >= 17000) continue;
            const size_t delayed = static_cast<size_t>(i - Separator::latency);
            maxReconstructionError = std::max(maxReconstructionError, std::abs(sum[0] - inputLeft[delayed]));
            maxReconstructionError = std::max(maxReconstructionError, std::abs(sum[1] - inputRight[delayed]));
            voiceEnergy += static_cast<double>(stems[1][0]) * stems[1][0]
                         + static_cast<double>(stems[1][1]) * stems[1][1];
            inputEnergy += static_cast<double>(dry[0]) * dry[0]
                         + static_cast<double>(dry[1]) * dry[1];
        }
        assert(maxReconstructionError < 2.0e-5f);
        assert(inputEnergy > 1.0);
        return voiceEnergy / inputEnergy;
    };
    const double centred = run(0);
    const double antiPhase = run(1);
    const double panned = run(2);
    std::cout << "stereo tone voice energy ratio: centre=" << centred
              << " anti-phase=" << antiPhase << " hard-left=" << panned << "\n";
    assert(centred > 0.05); // The probe should actually exercise voice masking.
    assert(antiPhase > centred * 0.25); // Formerly exactly zero due to mid-only masking.
    assert(panned > centred * 0.20);
}

void transportAndModeTests() {
    SpectralAnalyzer spectrum;
    Separator separator(spectrum);
    separator.prepare(48000.0);
    constexpr int n=13000;
    std::array<float,n> left{}, right{}, blend{};
    for(int i=0;i<n;++i) {
        left[static_cast<size_t>(i)]=i==0 ? 0.85f : 0.17f*std::sin(0.08f*i);
        right[static_cast<size_t>(i)]=i==0 ? -0.60f : 0.14f*std::cos(0.17f*i);
        // Simulate continuous Mono/Stereo interpolation and abrupt quality changes.
        blend[static_cast<size_t>(i)]=i<1800 ? 0.0f : (i<2400 ? (i-1800.0f)/600.0f : (i<6200 ? 1.0f : 0.32f));
        Separator::Stems stems{};
        std::array<float,2> dry{};
        separator.processSample(left[static_cast<size_t>(i)],right[static_cast<size_t>(i)],
                                blend[static_cast<size_t>(i)],(i/211)%3,stems,dry);
        std::array<float,2> sum{};
        for(int stem=0;stem<3;++stem) for(int c=0;c<2;++c)
            sum[static_cast<size_t>(c)]+=stems[static_cast<size_t>(stem)][static_cast<size_t>(c)];
        float expectedL=0,expectedR=0,expectedDryL=0,expectedDryR=0;
        if(i>=Separator::latency) {
            const auto j=static_cast<size_t>(i-Separator::latency);
            const float mid=0.5f*(left[j]+right[j]);
            expectedL=left[j]+blend[j]*(mid-left[j]);
            expectedR=right[j]+blend[j]*(mid-right[j]);
            expectedDryL=left[j];
            expectedDryR=right[j];
        }
        assert(std::abs(sum[0]-expectedL)<2.0e-5f);
        assert(std::abs(sum[1]-expectedR)<2.0e-5f);
        assert(std::abs(dry[0]-expectedDryL)<1.0e-7f);
        assert(std::abs(dry[1]-expectedDryR)<1.0e-7f);
        if(i<Separator::latency) {
            for(const auto& stem:stems)
                for(float v:stem) assert(v==0.0f); // initial-frame pre-echo guard
        }
    }
    separator.reset();
    for(int i=0;i<Separator::latency;++i) {
        Separator::Stems s{};
        std::array<float,2> delayed{};
        separator.processSample(0.0f,0.0f,0.0f,1,s,delayed);
        for(const auto& stem:s) for(float value:stem) assert(value==0.0f);
    }
}

void mixerTests() {
    // Saved project starts in bypass: first sample must equal delayed input.
    StemMixer saved;
    saved.prepare(48000.0);
    saved.setTargets({-40.0f, -40.0f, -40.0f}, {false, false, false},
                     {true, true, true}, true);
    saved.snapToTargets();
    Separator::Stems silentStems {};
    assert(saved.mix(silentStems, {0.37f, -0.27f})[0] == 0.37f);
    StemMixer m;
    m.prepare(48000.0);
    Separator::Stems stems{};
    for(int s=0;s<3;++s) {stems[static_cast<size_t>(s)][0]=0.2f;stems[static_cast<size_t>(s)][1]=-0.2f;}
    m.setTargets({0.0f,0.0f,0.0f},{false,false,false},{false,false,false},false);
    std::array<float,2> delayed{0.6f,-0.6f};
    for(int j=0;j<1200;++j) m.mix(stems,delayed);
    const auto all=m.mix(stems,delayed);
    assert(std::abs(all[0]-0.6f)<0.00001f);
    m.setTargets({0.0f,0.0f,0.0f},{false,true,false},{false,false,false},false);
    for(int j=0;j<1200;++j) m.mix(stems,delayed);
    const auto solo=m.mix(stems,delayed);
    assert(std::abs(solo[0]-0.2f)<0.00001f);
    m.setTargets({0.0f,0.0f,0.0f},{false,false,false},{false,false,false},true);
    for(int j=0;j<1200;++j) m.mix(stems,delayed);
    const auto bypass=m.mix(stems,delayed);
    assert(std::abs(bypass[0]-0.6f)<0.00001f);
}
}

int main() {
    maskTests();
    analyzerResetTests();
    mixerTests();
    transportAndModeTests();
    stereoPolarityTests();
    for (int sr : {44100, 48000, 96000, 192000}) voiceTailAndSilenceTests(sr);
    for(int sr:{44100,48000,96000}) for(int q=0;q<3;++q) for(bool mono:{false,true}) blockInvariantTests(sr,mono,q);
    std::cout<<"DSP regression tests passed\n";
}
