#include <cstring>

#include "engine/file.hpp"

#include "buffered-file.hpp"

BufferedFile::BufferedFile()
{

}

BufferedFile::~BufferedFile()
{
}

bool BufferedFile::open(std::string filename)
{
    bufferFilled = 0;

    file.open(filename);

    if(!file.is_open())
        return false;

    bufferFilled = file.read(0, bufferSize, reinterpret_cast<char *>(buffer));

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

    auto read = file.read(offset, bufferSize - bufferFilled, reinterpret_cast<char *>(buffer) + bufferFilled);

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