#include <cstring>

#include "engine/file.hpp"

#include "buffered-file.hpp"

BufferedFile::BufferedFile()
{

}

BufferedFile::~BufferedFile()
{
    if(file != -1)
        blit::close_file(file);
}

bool BufferedFile::open(std::string filename)
{
    bufferFilled = 0;

    file = blit::open_file(filename);

    if(file < 0)
        return false;

    bufferFilled = blit::read_file(file, 0, bufferSize, reinterpret_cast<char *>(buffer));

    if(bufferFilled <= 0)
        return false;

    offset = bufferFilled;

    return true;
}

void BufferedFile::read(int32_t len)
{
    if(len < bufferSize)
        memmove(buffer, buffer + len, bufferFilled - len);

    bufferFilled -= len;

    auto read = blit::read_file(file, offset, bufferSize - bufferFilled, reinterpret_cast<char *>(buffer) + bufferFilled);

    if(read <= 0)
        return;

    bufferFilled += read;
    offset += read;
}

int32_t BufferedFile::getBufferFilled() const
{
    return bufferFilled;
}

const uint8_t *BufferedFile::getBuffer() const
{
    return buffer;
}