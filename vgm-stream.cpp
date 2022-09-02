#include <cinttypes>
#include <cstring>

#include "vgm-stream.hpp"

#include "audio/audio.hpp"
#include "engine/engine.hpp"
#include "engine/file.hpp"

#ifdef PROFILER
#include "engine/profiler.hpp"
extern blit::ProfilerProbe *profilerRefillProbe;
extern blit::ProfilerProbe *profilerReadProbe;
extern blit::ProfilerProbe *profilerDecProbe;
#endif


VGMStream::VGMStream()
{

}

VGMStream::~VGMStream()
{

}

bool VGMStream::load(std::string filename)
{
    if(channel != -1)
        blit::channels[channel].off();

    currentSample = nullptr;
    bufferedSamples = 0;
    needConvert = false;
    supported = true;

    auto durationSamples = 1;


    // get info
    channels = 1;
    sampleRate = 22050;

    needConvert = sampleRate != 22050;
    supported = sampleRate % 22050 == 0;

    durationMs = (durationSamples * 1000) / sampleRate;

    // comments/tags
    tags = MusicTags();

    return true;
}

void VGMStream::play(int channel)
{
    this->channel = channel;

    if(!currentSample)
    {
        decode(0);
        decode(1);

        curAudioBuf = 0;
        currentSample = audioBuf[0];
        endSample = currentSample + dataSize[0];
        blit::channels[channel].wave_buf_pos = 0;
    }

    blit::channels[channel].waveforms = blit::Waveform::WAVE;
    blit::channels[channel].user_data  = this;
    blit::channels[channel].wave_buffer_callback = &VGMStream::staticCallback;

    blit::channels[channel].trigger_sustain();
}

void VGMStream::pause()
{
    blit::channels[channel].off();
}

bool VGMStream::getPlaying() const
{
    return channel != -1 && blit::channels[channel].adsr_phase == blit::ADSRPhase::SUSTAIN;
}

void VGMStream::update()
{
    if(!currentSample)
        return;

    // refill audio buffers
    for(int i = 0; i < 2; i++)
    {
        if(!dataSize[i])
            decode(i);
    }
}

int VGMStream::getCurrentSample() const
{
    if(channel == -1)
        return 0;

    return bufferedSamples + blit::channels[channel].wave_buf_pos;
}

int VGMStream::getDurationMs() const
{
    return durationMs;
}

const MusicTags &VGMStream::getTags() const
{
    return tags;
}

bool VGMStream::getFileSupported() const
{
    return supported;
}

void VGMStream::decode(int bufIndex)
{
#ifdef PROFILER
    blit::ScopedProfilerProbe scopedProbe(profilerRefillProbe);
#endif

    int samples = 0;
    int freqScale = sampleRate / 22050;

    /*if(needConvert)
    {
        while(samples < audioBufSize)
        {
            int16_t tmpBuf[audioBufSize];
            const int maxSize = audioBufSize;
            const int size = std::min(maxSize, (maxSize - samples) * freqScale);

            short *buf[]{tmpBuf};
            int tmpSamples = stb_vorbis_get_samples_short(vorbis, 1, buf, size);

            if(!tmpSamples)
                break;

            for(int i = 0; i < tmpSamples; i += freqScale, samples++)
            {
                int32_t tmp = 0;
                for(int j = 0; j < freqScale && i + j < tmpSamples; j++)
                    tmp += tmpBuf[i + j];
                audioBuf[bufIndex][samples] = tmp / freqScale;
            }
        }
    }
    else
    {
        short *buf[]{audioBuf[bufIndex]};
        samples = stb_vorbis_get_samples_short(vorbis, 1, buf, audioBufSize);
    }*/

    if(!samples)
    {
        dataSize[bufIndex] = -1;
        return;
    }

    // pad samples, should only happen at the end of the file
    while(samples % 64)
        audioBuf[bufIndex][samples++] = 0;

    dataSize[bufIndex] = samples;
}

void VGMStream::staticCallback(blit::AudioChannel &channel)
{
    reinterpret_cast<VGMStream *>(channel.user_data)->callback(channel);
}

void VGMStream::callback(blit::AudioChannel &channel)
{
    if(!currentSample)
    {
        channel.off();
        return;
    }

    // there was no buffer last time
    if(currentSample == endSample)
    {
        if(dataSize[curAudioBuf])
        {
            // recover from underrun
            endSample = audioBuf[curAudioBuf] + dataSize[curAudioBuf];
        }
        else
        {
            memset(channel.wave_buffer, 0, 64 * sizeof(int16_t));
            return;
        }
    }

    auto out = channel.wave_buffer;

    int i = 0;
    for(; i < 64; i++)
        *(out++) = *(currentSample++);

    // swap buffers
    if(currentSample == endSample)
    {
        dataSize[curAudioBuf] = 0;
        curAudioBuf = ++curAudioBuf % 2;

        if(dataSize[curAudioBuf] == -1) // EOF
            currentSample = endSample = nullptr;
        else
        {
            currentSample = audioBuf[curAudioBuf];
            endSample = currentSample + dataSize[curAudioBuf];

            // if(currentSample == endSample)
            // no more samples available - underrun
        }
    }

    bufferedSamples += 64;
}

