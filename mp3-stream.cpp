#include <cinttypes>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3

#include "mp3-stream.hpp"

#include "audio/audio.hpp"
#include "engine/engine.hpp"
#include "engine/file.hpp"

#ifdef PROFILER
#include "engine/profiler.hpp"
extern blit::ProfilerProbe *profilerRefillProbe;
extern blit::ProfilerProbe *profilerReadProbe;
extern blit::ProfilerProbe *profilerDecProbe;
#endif

// ID3v2 helpers

static int getSynchsafe(char *buf)
{
    auto ubuf = reinterpret_cast<uint8_t *>(buf);
    return ubuf[3] | (ubuf[2] << 7) | (ubuf[1] << 14) | (ubuf[0] << 21);
}

static uint32_t getInt32(char *buf)
{
    auto ubuf = reinterpret_cast<uint8_t *>(buf);
    return ubuf[3] | (ubuf[2] << 8) | (ubuf[1] << 16) | (ubuf[0] << 24);
}

static std::string readTextTag(int32_t file, uint32_t offset, uint32_t len)
{
    char encoding;
    blit::read_file(file, offset, 1, &encoding);
    std::string ret;

    if(encoding == 0)
    {
        // ISO-8859-1
        ret.resize(len - 1);

        std::vector<char> tmp(len - 1);
        blit::read_file(file, offset + 1, len - 1, tmp.data());

        // "convert" by throwing away anything non-ascii
        int i = 0;
        for(auto c : tmp)
        {
            if((c & 0x80) == 0)
                ret[i++] = c;
        }

        ret.resize(i);
    }
    else if(encoding == 1)
    {
        // UCS-2
        ret.resize(len - 3);

        uint16_t bom;
        std::vector<uint16_t> uChars((len - 3) / 2);
        blit::read_file(file, offset + 1, 2, reinterpret_cast<char *>(&bom));
        blit::read_file(file, offset + 3, len - 3, reinterpret_cast<char *>(uChars.data()));

        // "convert" by throwing away anything non-ascii
        int i = 0;
        for(auto c : uChars)
        {
            if(bom == 0xFEFF && c < 0x80)
                ret[i++] = c;
            else if((c >> 8) < 0x80)
                ret[i++] = c >> 8;
        }

        ret.resize(i);
    }
    else if(encoding == 3)
    {
        // UTF-8
        ret.resize(len - 1);

        std::vector<char> tmp(len - 1);
        blit::read_file(file, offset + 1, len - 1, tmp.data());

        // "convert" by throwing away anything non-ascii
        int i = 0;
        for(auto c : tmp)
        {
            if((c & 0x80) == 0)
                ret[i++] = c;
        }

        ret.resize(i);
    }
    else
        ret = "enc? " + std::to_string(encoding);

    return ret;
}

MP3Stream::MP3Stream()
{

}

bool MP3Stream::load(std::string filename)
{
    if(channel != -1)
        blit::channels[channel].off();

    currentSample = nullptr;
    bufferedSamples = 0;
    needConvert = false;
    supported = true;

    if(!audioFile.open(filename))
        return false;

    // TODO: we're opening the file three times here
    mp3dec_init(&mp3dec);
    durationMs = calcDuration(filename);

    tags = parseTags(filename);

    // start the decoder
    mp3dec_init(&mp3dec);

    return true;
}

MP3Stream::Tags MP3Stream::parseTags(std::string filename)
{
    Tags ret;

    auto file = blit::open_file(filename);

    if(file == -1)
        return ret;

    char buf[10];
    blit::read_file(file, 0, 10, buf);

    if(memcmp(buf, "ID3", 3) != 0)
    {
        blit::close_file(file);
        return ret;
    }

    // version/flags
    int versionMajor = buf[3], versionMinor = buf[4];
    int flags = buf[5];

    // size
    uint32_t size = getSynchsafe(buf + 6);

    printf("ID3v2.%i.%i flags %x size %" PRIi32 "\n", versionMajor, versionMinor, flags, size);

    uint32_t offset = 10;

    // skip extended header
    if(flags & 0x40)
    {
        blit::read_file(file, offset, 4, buf);
        offset += getSynchsafe(buf);
    }

    while(offset < size)
    {
        // read header
        blit::read_file(file, offset, 10, buf);

        if(buf[0] == 0)
            break;

        std::string id(buf, 4);
        uint32_t frameSize = versionMajor == 4 ? getSynchsafe(buf + 4) : getInt32(buf + 4);

        offset += 10;

        if(id == "TALB")
            ret.album = readTextTag(file, offset, frameSize);
        else if(id == "TIT2")
           ret.title = readTextTag(file, offset, frameSize);
        else if(id == "TPE1")
            ret.artist = readTextTag(file, offset, frameSize);
        else if(id == "TRCK")
            ret.track = readTextTag(file, offset, frameSize);
        else
            printf("\t%s size %" PRIu32 " flags %x %x @%" PRIx32 "\n", id.c_str(), frameSize, buf[8], buf[9], offset);

        offset += frameSize;
    }

    blit::close_file(file);
    return ret;
}

void MP3Stream::play(int channel)
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
    blit::channels[channel].callback_waveBufferRefresh = &MP3Stream::staticCallback;

    //blit::channels[channel].trigger_attack();
    blit::channels[channel].adsr = 0xFFFF00;
    blit::channels[channel].trigger_sustain();
}

void MP3Stream::pause()
{
    blit::channels[channel].off();
}

bool MP3Stream::getPlaying() const
{
    return channel != -1 && blit::channels[channel].adsr_phase == blit::ADSRPhase::SUSTAIN;
}

void MP3Stream::update()
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

int MP3Stream::getCurrentSample() const
{
    return bufferedSamples + blit::channels[channel].wave_buf_pos;
}

int MP3Stream::getDurationMs() const
{
    return durationMs;
}

const MP3Stream::Tags &MP3Stream::getTags() const
{
    return tags;
}

bool MP3Stream::getFileSupported() const
{
    return supported;
}

void MP3Stream::decode(int bufIndex)
{
#ifdef PROFILER
    blit::ScopedProfilerProbe scopedProbe(profilerRefillProbe);
#endif

    mp3dec_frame_info_t info = {};

    int samples = 0;
    int freqScale = 1;

    do
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

        if(needConvert)
        {
            // attempt to convert to mono 22050Hz (badly)
            int16_t tmpBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];
            int tmpSamples = mp3dec_decode_frame(&mp3dec, audioFile.getBuffer(), audioFile.getBufferFilled(), tmpBuf, &info);
            
            if(tmpSamples)
            {
                freqScale = info.hz / 22050;
                int div = info.channels * freqScale;

                for(int i = 0; i < tmpSamples * info.channels; i += div, samples++)
                {
                    int32_t tmp = 0;
                    for(int j = 0; j < div; j++)
                        tmp += tmpBuf[i + j];
                    
                    audioBuf[bufIndex][samples] = tmp / div;
                }
            }
        }
        else
            samples += mp3dec_decode_frame(&mp3dec, audioFile.getBuffer(), audioFile.getBufferFilled(), audioBuf[bufIndex] + samples, &info);

#ifdef PROFILER
        profilerDecProbe->Pause();
#endif

        // switch conversion on and retry if needed
        if(!needConvert && (info.channels != 1 || info.hz != 22050))
        {
            needConvert = true;
            decode(bufIndex);

            supported = info.hz % 22050 == 0;
            return;
        }
    
#ifdef PROFILER
        profilerReadProbe->Start();
#endif

        audioFile.read(info.frame_bytes);

#ifdef PROFILER
        profilerReadProbe->Pause();
#endif

        // / 2 because we only ever store mono
    } while (samples + (MINIMP3_MAX_SAMPLES_PER_FRAME / (2 * freqScale)) <= audioBufSize);

#ifdef PROFILER
    profilerReadProbe->StoreElapsedUs();
    profilerDecProbe->StoreElapsedUs();
#endif

    dataSize[bufIndex] = samples;
}

void MP3Stream::staticCallback(void *arg)
{
    reinterpret_cast<MP3Stream *>(arg)->callback();
}

void MP3Stream::callback()
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
    for(; i < 64 /*&& currentSample != endSample*/; i++)
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

    //for(; i < 64 && currentSample != endSample; i++)
    //    *(out++) = *(currentSample++);

    bufferedSamples += 64;
}

int MP3Stream::calcDuration(std::string filename)
{
    // decode entire file to get length
    BufferedFile file;
    if(!file.open(filename))
        return 0;

    unsigned int samples = 0;

    mp3dec_frame_info_t info = {};

    //while(true)
    while(file.getBufferFilled())
    {
        samples += mp3dec_decode_frame(&mp3dec, file.getBuffer(), file.getBufferFilled(), nullptr, &info);
        file.read(info.frame_bytes);
    }

    int lenMs = (static_cast<uint64_t>(samples) * 1000) / info.hz;
    return lenMs;
}