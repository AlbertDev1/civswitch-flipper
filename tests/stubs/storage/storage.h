#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { int unused; } Storage;
typedef struct { FILE* fp; } File;
enum { FSAM_READ, FSOM_OPEN_EXISTING };
File* storage_file_alloc(Storage* storage);
void storage_file_free(File* file);
bool storage_file_open(File* file, const char* path, int access, int mode);
void storage_file_close(File* file);
size_t storage_file_read(File* file, void* data, size_t size);
bool storage_file_seek(File* file, uint32_t offset, bool absolute);
uint64_t storage_file_size(File* file);
