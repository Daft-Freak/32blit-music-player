#include <cinttypes>

#include "vorbis-stream.hpp"

#include "audio/audio.hpp"
#include "engine/engine.hpp"
#include "engine/file.hpp"

#ifdef PROFILER
#include "engine/profiler.hpp"
extern blit::ProfilerProbe *profilerRefillProbe;
extern blit::ProfilerProbe *profilerReadProbe;
extern blit::ProfilerProbe *profilerDecProbe;
#endif

#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

VorbisStream::VorbisStream()
{

}

VorbisStream::~VorbisStream()
{
    if(vorbis)
        stb_vorbis_close(vorbis);
}

bool VorbisStream::load(std::string filename)
{
    if(channel != -1)
        blit::channels[channel].off();

    currentSample = nullptr;
    bufferedSamples = 0;
    needConvert = false;
    supported = true;

    if(vorbis)
    {
        stb_vorbis_close(vorbis);
        vorbis = nullptr;
    }

    if(!audioFile.open(filename))
        return false;

    // TODO: we're opening the file twice here

    // just to throw everyone off, this function returns samples (unlike the MP3Stream version)
    auto durationSamples = calcDuration(filename);

    // init decoder
    // need a big buffer for this
    int used = 0, error = 0;

    int32_t bufferLen = 1024 * 8;
    auto buffer = new uint8_t[bufferLen];
    auto bufferFilled = std::min(audioFile.getBufferFilled(), bufferLen);

    memcpy(buffer, audioFile.getBuffer(), bufferFilled);
    int lastRead = bufferFilled;

    while(!vorbis)
    {
        vorbis = stb_vorbis_open_pushdata(buffer, bufferFilled, &used, &error, nullptr);

        if(!vorbis && error == VORBIS_need_more_data)
        {
            if(bufferFilled == bufferLen)
            {
                auto newBuf = new uint8_t[bufferLen * 2];
                memcpy(newBuf, buffer, bufferFilled);

                delete[] buffer;
                buffer = newBuf;

                bufferLen *= 2;
            }

            audioFile.read(lastRead);
            int len = std::min(audioFile.getBufferFilled(), bufferLen - bufferFilled);
            memcpy(buffer + bufferFilled, audioFile.getBuffer(), len);
            bufferFilled += len;
            lastRead = len;
        }
        else if(!vorbis)
            break;
    }

    audioFile.read(lastRead - (bufferFilled - used));

    delete[] buffer;

    if(!vorbis)
        return false;

    // get info
    auto info = stb_vorbis_get_info(vorbis);
    channels = info.channels;
    sampleRate = info.sample_rate;

    needConvert = sampleRate != 22050;
    supported = sampleRate % 22050 == 0;

    durationMs = (durationSamples * 1000) / sampleRate;

    lastOutput = nullptr;
    lastOutputSamples = 0;

    // comments/tags
    tags = MusicTags();

    auto comments = stb_vorbis_get_comment(vorbis);

    for(int i = 0; i < comments.comment_list_length; i++)
    {
        std::string commentStr(comments.comment_list[i]);

        auto equals = commentStr.find('=');
        if(equals == std::string::npos)
            continue;

        auto key = commentStr.substr(0, equals);
        //auto value = commentStr.substr(equals + 1); // don't do this here to avoid oom on cover art

        if(key == "ALBUM")
            tags.album = commentStr.substr(equals + 1);
        else if(key == "ARTIST")
            tags.artist = commentStr.substr(equals + 1);
        else if(key == "TITLE")
            tags.title = commentStr.substr(equals + 1);
        else if(key == "TRACK")
            tags.track = commentStr.substr(equals + 1);
        else if(commentStr.length() < 1024 * 8) // avoid touching huge strings
            printf("%s: %s\n", key.c_str(), commentStr.substr(equals + 1).c_str());
    }

    return true;
}

/*MusicTags VorbisStream::parseTags(std::string filename)
{
    MusicTags ret;

    auto file = blit::open_file(filename);

    if(file == -1)
        return ret;

    //TODO: standalone tag parsing

    blit::close_file(file);
    return ret;
}*/

void VorbisStream::play(int channel)
{
    if(!audioFile.getBufferFilled())
        return;

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
    blit::channels[channel].volume = 0xFF;
    blit::channels[channel].wave_callback_arg = this;
    blit::channels[channel].callback_waveBufferRefresh = &VorbisStream::staticCallback;

    //blit::channels[channel].trigger_attack();
    blit::channels[channel].adsr = 0xFFFF00;
    blit::channels[channel].trigger_sustain();
}

void VorbisStream::pause()
{
    blit::channels[channel].off();
}

bool VorbisStream::getPlaying() const
{
    return channel != -1 && blit::channels[channel].adsr_phase == blit::ADSRPhase::SUSTAIN;
}

void VorbisStream::update()
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

int VorbisStream::getCurrentSample() const
{
    if(channel == -1)
        return 0;

    return bufferedSamples + blit::channels[channel].wave_buf_pos;
}

int VorbisStream::getDurationMs() const
{
    return durationMs;
}

const MusicTags &VorbisStream::getTags() const
{
    return tags;
}

bool VorbisStream::getFileSupported() const
{
    return supported;
}

void VorbisStream::decode(int bufIndex)
{
#ifdef PROFILER
    blit::ScopedProfilerProbe scopedProbe(profilerRefillProbe);
#endif

    int samples = 0;
    int freqScale = sampleRate / 22050;

    while(true)
    {
        if(audioFile.getBufferFilled() == 0)
        {
            //printf("end %i\n", read);
            currentSample = nullptr;
            break;
        }

#ifdef PROFILER
        profilerDecProbe->Start();
#endif

        float **outputs;
        int tmpSamples;

        if(lastOutput)
        {
            // re-use the output form last time
            tmpSamples = lastOutputSamples;
            outputs = lastOutput;
            lastOutput = nullptr;
        }
        else
        {
            int used = stb_vorbis_decode_frame_pushdata(vorbis, audioFile.getBuffer(), audioFile.getBufferFilled(), nullptr, &outputs, &tmpSamples);

            if(!used) // FIXME: actually handle not enough data
            {
                currentSample = nullptr;
                break;
            }

#ifdef PROFILER
            profilerReadProbe->Start();
#endif

            audioFile.read(used);

#ifdef PROFILER
            profilerReadProbe->Pause();
#endif
        }

        // can't fit next output, save it for later
        if(samples + tmpSamples / freqScale > audioBufSize)
        {
            lastOutput = outputs;
            lastOutputSamples = tmpSamples;
            break;
        }

        if(needConvert)
        {
            auto tmpBuf = new int16_t[tmpSamples];

            short *buf[]{tmpBuf};
            convert_samples_short(1, buf, 0, channels, outputs, 0, tmpSamples);

            for(int i = 0; i < tmpSamples; i += freqScale, samples++)
            {
                int32_t tmp = 0;
                for(int j = 0; j < freqScale && i + j < tmpSamples; j++)
                    tmp += tmpBuf[i + j];
                
                audioBuf[bufIndex][samples] = tmp / freqScale;
            }

            delete[] tmpBuf;
        }
        else
        {
            // FIXME: this is an internal function
            short *buf[]{audioBuf[bufIndex]};
            convert_samples_short(1, buf, samples, channels, outputs, 0, tmpSamples);
            samples += tmpSamples;
        }


#ifdef PROFILER
        profilerDecProbe->Pause();
#endif

    }

#ifdef PROFILER
    profilerReadProbe->StoreElapsedUs();
    profilerDecProbe->StoreElapsedUs();
#endif

    dataSize[bufIndex] = samples;
}

void VorbisStream::staticCallback(void *arg)
{
    reinterpret_cast<VorbisStream *>(arg)->callback();
}

void VorbisStream::callback()
{
    if(!currentSample)
    {
        blit::channels[channel].off();
        return;
    }

    // there was no buffer last time
    if(currentSample == endSample)
    {
        if(dataSize[curAudioBuf])
        {
            endSample = audioBuf[curAudioBuf] + dataSize[curAudioBuf];
            blit::debug("recovered\n");
        }
        else
        {
            memset(blit::channels[channel].wave_buffer, 0, 64 * sizeof(int16_t));
            return;
        }
    }

    auto out = blit::channels[channel].wave_buffer;

    int i = 0;
    for(; i < 64 && currentSample != endSample; i++)
        *(out++) = *(currentSample++);


    // swap buffers
    if(currentSample == endSample)
    {
        dataSize[curAudioBuf] = 0;
        curAudioBuf = ++curAudioBuf % 2;
        currentSample = audioBuf[curAudioBuf];
        endSample = currentSample + dataSize[curAudioBuf];

        if(currentSample == endSample)
            blit::debug("underrun!\n");
    }

    for(; i < 64 && currentSample != endSample; i++)
        *(out++) = *(currentSample++);

    bufferedSamples += 64;
}

uint64_t VorbisStream::calcDuration(std::string filename)
{
    // scan through the file backwards to find the sample pos of the last page

    auto file = blit::open_file(filename);
    if(file == -1)
        return 0;

    auto length = blit::get_file_length(file);
    const int chunkLen = 1024;
    
    for(uint32_t offset = chunkLen; offset < length; offset += chunkLen - 14)
    {
        uint8_t buf[chunkLen];
        blit::read_file(file, length - offset, chunkLen, reinterpret_cast<char *>(buf));

        for(int i = chunkLen - 14; i >= 0; i--)
        {
            if(memcmp(buf + i, "OggS", 4) == 0)
            {
                // skip two bytes, get 64bit value
                uint64_t sampleOff = 
                    buf[i + 6] |
                    (buf[i + 7] << 8) |
                    (buf[i + 8] << 16) |
                    (buf[i + 9] << 24) |
                    (static_cast<uint64_t>(buf[i + 10]) << 32) |
                    (static_cast<uint64_t>(buf[i + 11]) << 40) |
                    (static_cast<uint64_t>(buf[i + 12]) << 48) |
                    (static_cast<uint64_t>(buf[i + 13]) << 56);

                blit::close_file(file);
                return sampleOff;
            }
        }
    }

    blit::close_file(file);

    return 0;
}