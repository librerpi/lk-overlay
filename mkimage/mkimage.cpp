#include <arpa/inet.h>
#include <assert.h>
#include <iostream>
#include <lib/mincrypt/sha256.h>
#include <nlohmann/json.hpp>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

using namespace std;
using json = nlohmann::json;

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

void write_entry(FILE *out, const vector<uint8_t> &buffer, uint32_t magic, uint offset) {
  magic = htonl(magic);
  int32_t length = buffer.size();
  uint32_t be_length = htonl(length);
  fseek(out, offset, SEEK_SET);
  int n = fwrite(&magic, 4, 1, out);
  assert(n == 1);
  n = fwrite(&be_length, 4, 1, out);
  assert(n == 1);
  n = fwrite(buffer.data(), 1, length, out);
  assert(n == length);
  fflush(out);
}

vector<uint8_t> readFile(const string &filename, const vector<string> &includePath) {
  vector<uint8_t> output;
  FILE *handle;
  const char *fname = filename.c_str();
  if (fname[0] == '/') { // absolute path
    handle = fopen(filename.c_str(), "r");
  } else {
    for (const auto &dir : includePath) {
      handle = fopen((dir + string("/") + filename).c_str(), "r");
      if (handle) break;
    }
  }
  if (!handle) {
    fprintf(stderr, "error, cant find %s\n", filename.c_str());
    exit(1);
  }

  fseek(handle, 0, SEEK_END);
  uint32_t length = ftell(handle);
  fseek(handle, 0, SEEK_SET);

  output.resize(length);
  int n = fread(output.data(), length, 1, handle);
  assert(n == 1);
  fclose(handle);

  return output;
}

vector<uint8_t> hash_buffer(const vector<uint8_t> &buffer) {
  uint8_t hash[SHA256_DIGEST_SIZE];
  SHA256_hash(buffer.data(), buffer.size(), hash);
  return vector<uint8_t>(hash, hash + SHA256_DIGEST_SIZE);
}

int main(int argc, char **argv) {
  int opt;
  vector<string> includePath;
  includePath.push_back(".");
  string jsonPath;
  while ((opt = getopt(argc, argv, "-I:")) != -1) {
    switch (opt) {
    case 1:
      jsonPath = optarg;
      break;
    case 'I':
      printf("will search %s\n", optarg);
      includePath.push_back(optarg);
      break;
    }
  }


  vector<uint8_t> configJsonVec = readFile(jsonPath, includePath);
  configJsonVec.push_back(0);
  string configJsonStr = (char*)configJsonVec.data();
  json config = json::parse(configJsonStr);

  FILE *out = fopen("eeprom.bin", "wb");
  assert(out);

  {
    void *buffer = malloc(4 * 1024 * 1024);
    memset(buffer, 0xff, 4*1024*1024);
    fwrite(buffer, 4 * 1024 * 1024, 1, out);
    free(buffer);
  }

  uint32_t offset = 0;

  {
    json bootcode = config["bootcode"];
    if (bootcode.is_string()) {
      vector<uint8_t> stage1_blob = readFile(bootcode, includePath);
      assert(offset == 0);
      write_entry(out, stage1_blob, 0x55aaf00f, offset);
      offset = ROUNDUP(offset + 8 + stage1_blob.size(), 8);
    }
  }

  {
    for (auto &elm : config["files"]) {
      string filename = elm["filename"];
      string magicStr = elm["magic"];
      int alignment = elm["alignment"];

      if (((alignment & (alignment - 1)) != 0) || (alignment < 8)) {
        printf("alignment of %d on file %s is invalid, it must be a power of 2 and over 8\n", alignment, filename.c_str());
      }

      uint32_t magic = strtol(magicStr.c_str(), 0, 16);
      cout << elm << endl;
      if ((offset % alignment) != 0) {
        uint32_t offset_goal = ROUNDUP(offset, alignment);
        printf("offset is 0x%x, but we need 0x%x alignment, bumping up to 0x%x\n", offset, alignment, offset_goal);
        uint32_t padding_size = offset_goal - offset;
        assert(padding_size >= 8);
        vector<uint8_t> padding(padding_size - 8, 0xff);
        write_entry(out, padding, 0x55aafeef, offset);
        offset = offset_goal;
      }
      vector<uint8_t> buffer = readFile(filename, includePath);
      if (magic == 0xaa55f11f) { // file with name
        string name = elm["name"];
        assert(name.size() < 16);
        // hash the buffer while it only has the file contents
        vector<uint8_t> hash = hash_buffer(buffer);

        // create a header with the name as a char[16]
        vector<uint8_t> nameHeader(name.begin(), name.end());
        nameHeader.resize(16);

        // insert the name at the start
        buffer.insert(buffer.begin(), nameHeader.begin(), nameHeader.end());
        // append the hash at the end
        buffer.insert(buffer.end(), hash.begin(), hash.end());
        // write to SPI
        write_entry(out, buffer, magic, offset);
      } else {
        write_entry(out, buffer, magic, offset);
      }
      offset = ROUNDUP(offset + 8 + buffer.size(), 8);
    }
  }

  printf("total space used in SPI image: %d KB\n", offset >> 10);

#if 0
  uint32_t stage2_offset = ROUNDUP(8 + length, 8);

  length = get_file(argv[2], &buffer);
  write_file(out, buffer, length, 0xaa55f00f, stage2_offset);
  free(buffer);
#endif

  fclose(out);
  return 0;
}
