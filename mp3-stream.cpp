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

static std::string readString(int32_t file, uint32_t offset, char encoding, int32_t len)
{
    std::string ret;

    if(encoding == 0)
    {
        // ISO-8859-1
        char c;
        blit::read_file(file, offset++, 1, &c);

        for(int i = 1; i < len || (len == -1 && c); i++)
        {
            // "convert" by throwing away anything non-ascii
            if((c & 0x80) == 0)
                ret += c;
            blit::read_file(file, offset++, 1, &c);
        }
    }
    else if(encoding == 1)
    {
        // UCS-2
        uint16_t bom;
        blit::read_file(file, offset, 2, reinterpret_cast<char *>(&bom));
        offset += 2;

        uint16_t c;
        blit::read_file(file, offset, 2, reinterpret_cast<char *>(&c));
        offset += 2;

        for(int i = 1; i < (len - 2) / 2 || (len == -1 && c); i++)
        {
            // "convert" by throwing away anything non-ascii
            if(bom == 0xFEFF && c < 0x80)
                ret += c;
            else if((c >> 8) < 0x80)
                ret += (c >> 8);

            blit::read_file(file, offset, 2, reinterpret_cast<char *>(&c));
            offset += 2;
        }
    }
    else if(encoding == 3)
    {
        // UTF-8
        char c;
        blit::read_file(file, offset++, 1, &c);

        for(int i = 1; i < len || (len == -1 && c); i++)
        {
            // "convert" by throwing away anything non-ascii
            if((c & 0x80) == 0)
                ret += c;
            blit::read_file(file, offset++, 1, &c);
        }
    }
    else
        ret = "enc? " + std::to_string(encoding);

    return ret;
}

static std::string readTextTag(int32_t file, uint32_t offset, int32_t len)
{
    char encoding;
    blit::read_file(file, offset, 1, &encoding);

    return readString(file, offset + 1, encoding, len);
}

//giant hack
#include "stm32h7xx_hal_mdma.h"
#include "stm32h7xx_hal_jpeg.h"
//

struct JPEGImage {
    uint32_t width, height;
    uint8_t *data;
};

static JPEG_HandleTypeDef jpeg_handle;
static uint8_t *jpeg_in_buf = nullptr, *jpeg_out_buf = nullptr;
static uint32_t jpeg_in_len = 0, jpeg_in_off = 0;
static uint32_t jpeg_out_len = 0, jpeg_out_off = 0;

void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef *hjpeg, JPEG_ConfTypeDef *pInfo) {  
    printf("InfoReadyCallback %i %i %i %i %i\n", pInfo->ImageWidth, pInfo->ImageHeight, pInfo->ImageQuality, pInfo->ColorSpace, pInfo->ChromaSubsampling);
}

void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t NbDecodedData) {
    printf("GetDataCallback %u %u %u\n", NbDecodedData, jpeg_in_off, jpeg_in_len);
    jpeg_in_off += NbDecodedData;

    if(jpeg_in_off < jpeg_in_len)
        HAL_JPEG_ConfigInputBuffer(&jpeg_handle, jpeg_in_buf + jpeg_in_off, jpeg_in_len - jpeg_in_off);  
}

void HAL_JPEG_DataReadyCallback (JPEG_HandleTypeDef *hjpeg, uint8_t *pDataOut, uint32_t OutDataLength) {
    printf("DataReadyCallback %u %u %u\n", OutDataLength, jpeg_out_off, jpeg_out_len);

    jpeg_out_off += OutDataLength;

    if(jpeg_out_off == jpeg_out_len) {
        auto new_buf = new uint8_t[jpeg_out_len + 1024];
        memcpy(new_buf, jpeg_out_buf, jpeg_out_len);
        delete[] jpeg_out_buf;
        jpeg_out_buf = new_buf;
        jpeg_out_len += 1024;
    }

    HAL_JPEG_ConfigOutputBuffer(&jpeg_handle, jpeg_out_buf + jpeg_out_off, jpeg_out_len - jpeg_out_off);
}

JPEGImage decode_jpeg_buf(uint8_t *ptr, uint32_t len)
{
    jpeg_in_buf = ptr;
    jpeg_in_len = len;
    jpeg_in_off = 0;

    jpeg_handle.Instance = JPEG;
    HAL_JPEG_Init(&jpeg_handle);

    jpeg_out_len = 1024;
    jpeg_out_buf = new uint8_t[jpeg_out_len];
    jpeg_out_off = 0;

    auto status = HAL_JPEG_Decode(&jpeg_handle, ptr, len, jpeg_out_buf, jpeg_out_len, 0xFFFFFFFF);

    JPEG_ConfTypeDef conf;
    HAL_JPEG_GetInfo(&jpeg_handle, &conf);

    printf("%i %i %i %i\n", conf.ImageWidth, conf.ImageHeight, conf.ImageQuality, conf.ColorSpace);

    HAL_JPEG_DeInit(&jpeg_handle);

    return {conf.ImageWidth, conf.ImageHeight, jpeg_out_buf};
}

static void decodeAPIC(int32_t file, uint32_t offset, uint32_t len)
{
    auto frameOffset = offset;
    char encoding;
    blit::read_file(file, offset++, 1, &encoding);

    std::string mime;
    char c;
    blit::read_file(file, offset++, 1, &c);

    while(c)
    {
        mime += c;
        blit::read_file(file, offset++, 1, &c);
    }

    char picType;
    blit::read_file(file, offset++, 1, &picType);

    auto desc = readString(file, offset, encoding, -1);
    int descLen = desc.length() + 1;

    if(encoding == 1) // wide chars
        descLen = (descLen + 1) * 2;

    offset += descLen;

    printf("pic %i mime %s desc %s\n", picType, mime.c_str(), desc.c_str());

    if(mime != "image/jpeg")
        return;

    auto picLen = len - (offset - frameOffset);
    char *picData = new char[picLen];
    blit::read_file(file, offset, picLen, picData);

    //
    decode_jpeg_buf((uint8_t *)picData, picLen);

    delete[] picData;
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
        else if(id == "APIC")
            decodeAPIC(file, offset, frameSize);
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