#pragma once
#include <cstdint>
#include <cstdio> // make sure stdio isn't included after this

#include "engine/file.hpp"

// wrap stdio funcs around blit:: funcs

struct wrap_FILE
{
    int32_t file;
    uint32_t offset;
};

inline wrap_FILE *wrap_fopen(const char *filename, const char *mode)
{
    auto ret = new wrap_FILE;

    ret->file = blit::open_file(filename);
    ret->offset = 0;

    if(ret->file == -1)
        return nullptr;
    
    return ret;
}

inline int wrap_fclose(wrap_FILE *file)
{
    return blit::close_file(file->file) == -1 ? EOF : 0;
}

inline size_t wrap_fread(void *buffer, size_t size, size_t count, wrap_FILE *file)
{
    auto ret = blit::read_file(file->file, file->offset, size * count, (char *)buffer);
    file->offset += ret;

    return ret < 0 ? 0 : ret / size;
}

inline int wrap_fgetc(wrap_FILE *file)
{
    char ret;
    auto read = blit::read_file(file->file, file->offset, 1, &ret);

    if(read)
    {
        file->offset++;
        return (uint8_t)ret;
    }

    return EOF;
}

inline int wrap_fseek(wrap_FILE *file, long offset, int origin)
{
    if(origin == SEEK_SET)
        file->offset = offset;
    else if(origin == SEEK_CUR)
        file->offset += offset;
    else
        file->offset = blit::get_file_length(file->file) - offset;

    return 0;
}

inline long wrap_ftell(wrap_FILE *file)
{
    return file->offset;
}

#define FILE wrap_FILE
#define fopen wrap_fopen
#define fclose wrap_fclose
#define fread wrap_fread
#define fgetc wrap_fgetc
#define fseek wrap_fseek
#define ftell wrap_ftell
