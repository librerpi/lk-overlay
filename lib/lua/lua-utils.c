#include <lib/fs.h>
#include <lua.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int pos; // offset into file
  filehandle *fd;
  char buff[512];
} lua_filehandle;

void lua_prettyprint(lua_State *L, int index) {
  const char *str;
  int type = lua_type(L, index);
  printf("%d: %s", index, lua_typename(L, type));
  switch (type) {
  case LUA_TSTRING:
    str = lua_tostring(L, index);
    printf(" == %s", str);
  }
  puts("");
}

static int lua_print(lua_State *L) {
  int argc = lua_gettop(L);
  for (int i=1; i<= argc; i++) {
    const char *str = lua_tostring(L, i);
    printf("%s", str);
  }
  puts("");
  return 0;
}

void register_globals(lua_State *L) {
  lua_register(L, "print", &lua_print);
}

typedef struct {
  const char *buffer;
  size_t size;
} mem_handle;

void init_memhandle_string(mem_handle *fd, const char *string) {
  fd->buffer = string;
  fd->size = strlen(string);
}

const char *memfs_read(lua_State *L, void *data, size_t *size) {
  mem_handle *fd = data;
  if (fd->size) {
    *size = fd->size;
    fd->size = 0;
    return fd->buffer;
  } else {
    *size = 0;
    return NULL;
  }
}

int luaL_loadstring(lua_State *L, const char *s) {
  mem_handle fd;
  init_memhandle_string(&fd, s);
  return lua_load(L, &memfs_read, &fd, "loadstring", NULL);
}

void* lua_allocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    free(ptr);
    return NULL;
  } else {
    return realloc(ptr, nsize);
  }
}

const char *lk_fs_read(lua_State *L, void *data, size_t *size) {
  lua_filehandle *fd = data;
  *size = fs_read_file(fd->fd, fd->buff, fd->pos, 512);
  fd->pos += *size;
  return fd->buff;
}

int luaL_loadfile(lua_State *L, const char *filename) {
  lua_filehandle fh;
  fh.pos = 0;
  int ret = fs_open_file(filename, &fh.fd);
  if (ret) {
    lua_pushfstring(L, "cannot open %s: %s", filename, ret);
    printf("failed to open %s: %d\n", filename, ret);
    return LUA_ERRERR;
  }
  ret = lua_load(L, &lk_fs_read, &fh, filename, NULL);
  fs_close_file(fh.fd);
  return ret;
}
