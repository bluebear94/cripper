#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <zlib.h>

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

char* getPWD();

#if defined(_WIN32)
#include <windows.h>
#define MKDIR(n) mkdir(n)
#define CHDIR(n) SetCurrentDirectory(n)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(n) mkdir(n, 0700)
#define CHDIR(n) chdir(n)
#endif

#define CHUNK 131072

#define FILE_COUNT_OFFSET 11
#define FNAME_SECTION_SIZE_OFFSET 16

int readArgs(int argc, char** argv, int* verbose, char** src, char** dest);
int32_t getInt(FILE* f);
int32_t getIntP(uint8_t* f);
int readZipped(FILE* f, uint8_t** block, int* amtReadC, int* amtReadU);
FILE* openFile(wchar_t* fname);
void wmkdir(wchar_t* dname);
