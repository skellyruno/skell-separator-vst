#pragma once
// Small testing-only API shim. The shipping plugin builds against actual JUCE;
// this shim makes core DSP regression tests executable without a JUCE checkout.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace juce {
    template<class T> T jmin(T a,T b) { return std::min(a,b); }
    template<class T> T jmax(T a,T b) { return std::max(a,b); }
    template<class T> T jlimit(T lo,T hi,T value) { return std::clamp(value,lo,hi); }
    template<class T> struct MathConstants {
        static constexpr T twoPi = static_cast<T>(6.283185307179586476925286766559);
    };
    namespace dsp {
        template <typename T> using Complex = std::complex<T>;
        class FFT {
        public:
            explicit FFT(int order) : n(1 << order), scratch(static_cast<size_t>(n)) {}
            void perform(const Complex<float>* input, Complex<float>* output, bool inverse) noexcept {
                for (int i=0;i<n;++i) scratch[static_cast<size_t>(i)]=input[i];
                for(int i=1,j=0;i<n;++i) {
                    int bit = n >> 1;
                    for (; j & bit; bit >>= 1) j ^= bit;
                    j ^= bit;
                    if(i<j) std::swap(scratch[static_cast<size_t>(i)],scratch[static_cast<size_t>(j)]);
                }
                for(int len=2;len<=n;len<<=1) {
                    float angle=static_cast<float>((inverse?1:-1)*MathConstants<float>::twoPi/len);
                    std::complex<float> step(std::cos(angle),std::sin(angle));
                    for(int base=0;base<n;base+=len) {
                        std::complex<float> w(1,0);
                        for(int j=0;j<len/2;++j) {
                            auto u=scratch[static_cast<size_t>(base+j)];
                            auto v=scratch[static_cast<size_t>(base+j+len/2)]*w;
                            scratch[static_cast<size_t>(base+j)]=u+v;
                            scratch[static_cast<size_t>(base+j+len/2)]=u-v;
                            w *= step;
                        }
                    }
                }
                float factor=inverse?1.0f/n:1.0f;
                for(int i=0;i<n;++i) output[i]=scratch[static_cast<size_t>(i)]*factor;
            }
        private:
            int n;
            std::vector<std::complex<float>> scratch;
        };
    }
    namespace ValueSmoothingTypes { struct Linear {}; }
    template<class T, class Smoothing = ValueSmoothingTypes::Linear>
    class SmoothedValue {
    public:
        void reset(double sampleRate,double seconds) {steps=static_cast<int>(sampleRate*seconds);remaining=0;}
        void setCurrentAndTargetValue(T value) {current=target=value;remaining=0;}
        void setTargetValue(T value) { if(value==target) return; target=value;remaining=steps; delta=remaining>0 ? (target-current)/remaining:0; if(remaining==0) current=target; }
        T getTargetValue() const { return target; }
        T getNextValue() {if(remaining>0) {current+=delta; --remaining; if(!remaining) current=target;} return current;}
    private:
        T current{},target{},delta{};
        int steps=0,remaining=0;
    };
    namespace Decibels { inline float decibelsToGain(float db) {return std::pow(10.0f,db/20.0f);} }
}
