/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <math.h>

#include "IntegerRatio.h"
#include "LinearResampler.h"
#include "MultiChannelResampler.h"
#include "PolyphaseResampler.h"
#include "PolyphaseResamplerMono.h"
#include "PolyphaseResamplerStereo.h"
#include "SincResampler.h"
#include "SincResamplerStereo.h"

using namespace resampler;

MultiChannelResampler::MultiChannelResampler(const MultiChannelResampler::Builder &builder)
        : mNumTaps(builder.getNumTaps())
        , mX(builder.getChannelCount() * builder.getNumTaps() * 2)
        , mSingleFrame(builder.getChannelCount())
        , mChannelCount(builder.getChannelCount())
        {}

MultiChannelResampler *MultiChannelResampler::make(int32_t channelCount,
                                                   int32_t inputRate,
                                                   int32_t outputRate,
                                                   Quality quality) {
    Builder builder;
    builder.setInputRate(inputRate);
    builder.setOutputRate(outputRate);
    builder.setChannelCount(channelCount);

    // TODO benchmark and review these numTaps
    switch (quality) {
        case Quality::Low:
            builder.setNumTaps(8);
            break;
        case Quality::Medium:
        default:
            builder.setNumTaps(16);
            break;
        case Quality::High:
            builder.setNumTaps(24);
            break;
        case Quality::Best:
            builder.setNumTaps(32);
            break;
    }

    // Set the cutoff frequency so that we do not get aliasing when down-sampling.
    if (outputRate < inputRate) {
        builder.setNormalizedCutoff((0.9f * outputRate) / inputRate);
    }
    return builder.build();
}

MultiChannelResampler *MultiChannelResampler::Builder::build() {
    IntegerRatio ratio(getInputRate(), getOutputRate());
    ratio.reduce();
    bool usePolyphase = (getNumTaps() * ratio.getDenominator()) <= kMaxCoefficients;
    if (usePolyphase) {
        if (getChannelCount() == 1) {
            return new PolyphaseResamplerMono(*this);
        } else if (getChannelCount() == 2) {
            return new PolyphaseResamplerStereo(*this);
        } else {
            return new PolyphaseResampler(*this);
        }
    } else {
        // Use less optimized resampler that uses a float phaseIncrement.
        // TODO mono resampler
        if (getChannelCount() == 2) {
            return new SincResamplerStereo(*this);
        } else {
            return new SincResampler(*this);
        }
    }
}

void MultiChannelResampler::writeFrame(const float *frame) {
    // Advance cursor before write so that cursor points to last written frame in read.
    if (++mCursor >= getNumTaps()) {
        mCursor = 0;
    }
    float *dest = &mX[mCursor * getChannelCount()];
    int offset = getNumTaps() * getChannelCount();
    for (int channel = 0; channel < getChannelCount(); channel++) {
        // Write twice so we avoid having to wrap when reading.
        dest[channel] = dest[channel + offset] = frame[channel];
    }
}

float MultiChannelResampler::hammingWindow(float radians, int spread) {
    const float alpha = 0.54f;
    const float windowPhase = radians / spread;
    return (float) (alpha + ((1.0 - alpha) * cosf(windowPhase)));
}

float MultiChannelResampler::sinc(float radians) {
    if (abs(radians) < 0.00000001) return 1.0f;   // avoid divide by zero
    return sinf(radians) / radians;   // Sinc function
}

// Unoptimized calculation used to construct lookup tables.
float MultiChannelResampler::calculateWindowedSinc(float radians, int spread) {
    return sinc(radians) * hammingWindow(radians, spread); // TODO try Kaiser window
}
