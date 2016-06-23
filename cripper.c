#include "cripper.h"

int main(int argc, char** argv) {
  int verbose = 0;
  char* src = NULL;
  char* dest = NULL;
  int stat = readArgs(argc, argv, &verbose, &src, &dest);
  if (stat != 0) return stat;
  FILE* input = fopen(src, "rb");
  if (input == NULL) {
    fprintf(stderr, "Could not open file %s\n", src);
    return -1;
  }
  fseek(input, FILE_COUNT_OFFSET, SEEK_SET);
  int32_t fileCount = getInt(input);
  fseek(input, FNAME_SECTION_SIZE_OFFSET, SEEK_SET);
  int32_t fnameSectionSize = getInt(input);
  uint8_t* fnameSection;
  int amtReadC, amtReadU;
  int status = readZipped(input, &fnameSection, &amtReadC, &amtReadU);
  assert(amtReadC == fnameSectionSize);
  if (status != 0) {
    fprintf(stderr, "Something went wrong with unzipping?\n");
    return status;
  }
  MKDIR(dest);
  char* oldpwd = getPWD();
  CHDIR(dest);
  int32_t offset = 0;
  if (verbose) printf("We have %d files to rip.\n", fileCount);
  for (int i = 0; i < fileCount; ++i) {
    int32_t entrySize = getIntP(fnameSection + offset);
    int32_t dirnameLength = getIntP(fnameSection + offset + 4);
    int32_t fni = offset + 8 + (dirnameLength << 1);
    int32_t filenameLength = getIntP(fnameSection + fni);
    int32_t fi = fni + 4 + (filenameLength << 1);
    int32_t isCompressed = getIntP(fnameSection + fi);
    int32_t uncompressedSize = getIntP(fnameSection + fi + 4);
    int32_t compressedSize = getIntP(fnameSection + fi + 8);
    int32_t fileOffset = getIntP(fnameSection + fi + 12);
    wchar_t* fullName = malloc(
      sizeof(wchar_t) * (dirnameLength + filenameLength + 1));
    wcsncpy(fullName, (wchar_t*) (fnameSection + offset + 8), dirnameLength);
    fullName[dirnameLength] = L'\0';
    wmkdir(fullName);
    wcsncpy(fullName + dirnameLength, (wchar_t*) (fnameSection + fni + 4), filenameLength);
    fullName[dirnameLength + filenameLength] = L'\0';
    if (verbose) {
      printf("%ls\n", fullName);
      if (isCompressed) printf("- Compressed size: %d\n", compressedSize);
      printf("- Uncompressed size: %d\n", uncompressedSize);
    }
    FILE* out = openFile(fullName);
    if (out == NULL) {
      printf("Could not open file %ls\n", fullName);
      free(fnameSection);
      free(oldpwd);
      fclose(input);
      free(fullName);
      return -1;
    }
    fseek(input, fileOffset, SEEK_SET);
    uint8_t* fileData = NULL;
    if (isCompressed) {
      readZipped(input, &fileData, &amtReadC, &amtReadU);
      assert(amtReadC == compressedSize);
      assert(amtReadU == uncompressedSize);
    } else {
      fileData = malloc(uncompressedSize);
      fread(fileData, uncompressedSize, 1, input);
    }
    fwrite(fileData, uncompressedSize, 1, out);
    fclose(out);
    free(fullName);
    offset += 4 + entrySize;
  }
  free(fnameSection);
  CHDIR(oldpwd);
  free(oldpwd);
  fclose(input);
}

int readArgs(int argc, char** argv, int* verbose, char** src, char** dest) {
  int ok = 1;
  for (int i = 1; i < argc; ++i) {
    char* arg = argv[i];
    if (arg[0] == '-') {
      if (arg[1] == '-') {
        if (!strcmp(arg + 2, "verbose")) *verbose = 1;
        else {
          ok = 0;
          break;
        }
      } else {
        for (int j = 1; arg[j] != '\0'; ++j) {
          switch (arg[j]) {
            case 'v': {
              *verbose = 1;
              break;
            }
            default: {
              ok = 0;
              break;
            }
          }
          if (!ok) break;
        }
      }
    }
    else if (*src == NULL) *src = arg;
    else if (*dest == NULL) *dest = arg;
    else {
      ok = 0;
      break;
    }
  }
  if (!ok || *dest == NULL) {
    fputs("Usage: cripper [--verbose] <source> <destination>", stderr);
    return -1;
  }
  return 0;
}

int32_t getInt(FILE* f) {
  uint8_t a = fgetc(f);
  uint8_t b = fgetc(f);
  uint8_t c = fgetc(f);
  uint8_t d = fgetc(f);
  return (int32_t) (a | (b << 8) | (c << 16) | (d << 24));
}

int32_t getIntP(uint8_t* f) {
  uint8_t a = f[0];
  uint8_t b = f[1];
  uint8_t c = f[2];
  uint8_t d = f[3];
  return (int32_t) (a | (b << 8) | (c << 16) | (d << 24));
}

int readZipped(FILE* f, uint8_t** block, int* amtReadC, int* amtReadU) {
  if (block == NULL) return -1;
  int bsize = 1;
  int ret = Z_OK;
  uint8_t* src = malloc(CHUNK);
  uint8_t* dest = malloc(bsize * CHUNK);
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  ret = inflateInit(&strm);
  if (ret != Z_OK) goto end;
  do {
    strm.avail_in = fread(src, 1, CHUNK, f);
    if (ferror(f)) {
      (void) inflateEnd(&strm);
      ret = Z_ERRNO;
      break;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = src;
    do {
      if (strm.total_out > CHUNK * (bsize - 1)) {
        bsize <<= 1;
        dest = realloc(dest, bsize * CHUNK);
      }
      strm.avail_out = CHUNK;
      strm.next_out = dest + strm.total_out;
      ret = inflate(&strm, Z_NO_FLUSH);
      if (ret == Z_STREAM_ERROR) goto end;
      switch (ret) {
      case Z_NEED_DICT:
          ret = Z_DATA_ERROR;     /* and fall through */
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
          (void) inflateEnd(&strm);
          goto end;
          break;
        default:
          ret = Z_OK;
      }
    } while (strm.avail_out == 0);
    printf("Unzip: total_out %lu available space %d\n", strm.total_out, CHUNK * bsize);
  } while (strm.avail_out != 0);
  end:
  *amtReadC = strm.total_in;
  *amtReadU = strm.total_out;
  free(src);
  if (ret == 0) *block = dest;
  else {
    free(dest);
    *block = NULL;
  }
  return ret;
}

#if defined(_WIN32)
FILE* openFile(wchar_t* fname) {
  return _wfopen(fname, L"wb");
}
char* getPWD() {
  size_t size = GetCurrentDirectory(NULL, 0);
  char* res = malloc(size + 1);
  GetCurrentDirectory(res, size);
  return res;
}
// http://stackoverflow.com/questions/2336242/recursive-mkdir-system-call-on-unix
// modified to work on wchar_t*'s
static void mkdirP(const wchar_t *dir) {
  wchar_t tmp[256];
  wchar_t *p = NULL;
  size_t len;
  memcpy(tmp, dir, sizeof(tmp));
  len = wcslen(tmp);
  if (tmp[len - 1] == L'/')
    tmp[len - 1] = 0;
  for (p = tmp + 1; *p; p++) {
    if (*p == L'/') {
      *p = 0;
      _wmkdir(tmp);
      *p = L'/';
    }
  }
  _wmkdir(tmp);
}
void wmkdir(wchar_t* dname) {
  mkdirP(dname);
}
#else
void writeChar(int c, char* dest, int* offsetP) {
  int offset = *offsetP;
  if (c < 0x80) {
    dest[offset] = (char) c;
    ++offset;
  }
  else if (c < 0x800) {
    dest[offset] = (char) (0xC0 | (c >> 6));
    dest[offset + 1] = (char) (0x80 | (c & 63));
    offset += 2;
  }
  else if (c < 0x10000) {
    dest[offset] = (char) (0xE0 | (c >> 12));
    dest[offset + 1] = (char) (0x80 | ((c >> 6) & 63));
    dest[offset + 2] = (char) (0x80 | (c & 63));
    offset += 3;
  }
  else if (c < 0x200000) {
    dest[offset] = (char) (0xF0 | (c >> 18));
    dest[offset + 1] = (char) (0x80 | ((c >> 12) & 63));
    dest[offset + 2] = (char) (0x80 | ((c >> 6) & 63));
    dest[offset + 3] = (char) (0x80 | (c & 63));
    offset += 4;
  }
  *offsetP = offset;
}
char* utf16To8(wchar_t* s) {
  int chars = wcslen(s);
  char* s8 = malloc((chars << 1) + 1);
  int offset = 0;
  for (int i = 0; i < chars; ++i) {
    wchar_t c = s[i];
    if (c >= 0xd800 && c < 0xdc00) {
      wchar_t c2 = s[++i];
      if (c2 >= 0xdc00 && c2 < 0xe000) {
        int cpoint = 0x10000 + (((c - 0xd800) << 16) | (c2 - 0xdc00));
        writeChar(cpoint, s8, &offset);
      } else {
        free(s8);
        return NULL;
      }
    } else if (c >= 0xdc00 && c < 0xe000) {
      free(s8);
      return NULL;
    } else {
      writeChar(c, s8, &offset);
    }
  }
  s8[offset] = '\0';
  return s8;
}
FILE* openFile(wchar_t* fname) {
  char* fnameUTF8 = utf16To8(fname);
  FILE* f = fopen(fnameUTF8, "wb");
  free(fnameUTF8);
  return f;
}
char* getPWD() {
  size_t size = 256;
  while (1) {
    char *buffer = (char*) malloc(size);
    if (getcwd(buffer, size) == buffer)
      return buffer;
    free(buffer);
    if (errno != ERANGE)
      return NULL;
    size <<= 1;
  }
}
// http://stackoverflow.com/questions/2336242/recursive-mkdir-system-call-on-unix
static void mkdirP(const char *dir) {
  char tmp[256];
  char *p = NULL;
  size_t len;
  snprintf(tmp, sizeof(tmp), "%s", dir);
  len = strlen(tmp);
  if (tmp[len - 1] == '/')
    tmp[len - 1] = 0;
  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      mkdir(tmp, S_IRWXU);
      *p = '/';
    }
  }
  mkdir(tmp, S_IRWXU);
}
void wmkdir(wchar_t* dname) {
  char* dnameUTF8 = utf16To8(dname);
  mkdirP(dnameUTF8);
  free(dnameUTF8);
}
#endif
