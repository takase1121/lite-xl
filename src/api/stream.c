#include <stdio.h>
#include <SDL.h>
#include <lua.h>
#include <lauxlib.h>
#include <string.h>

#include "stream.h"

#ifdef _WIN32
typedef DWORD stream_error_t;
#else
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
typedef int stream_error_t;

#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#define DEFAULT_BUF_SIZE 4096

// using bip buffer based on https://www.stefanmisik.com/post/bip-buffer-made-easy.html
typedef struct {
#ifdef _WIN32
  OVERLAPPED overlapped;
#endif
  uint32_t size, used, head, tail, current_size;
  uint32_t pending;
  uint8_t *data;
} stream_buf_t;

struct stream_s {
  SDL_mutex *mtx;
  stream_flag_t flags;
  stream_handle_t handle;
  stream_error_t last_error;
  stream_finalizer_fn_t finalizer;
  void *finalizer_userdata;
  stream_buf_t buf[2];
};

#define API_TYPE_STREAM "Stream"
#define API_TYPE_SOCKET "Socket"

#ifdef _WIN32
static HANDLE iocp_handle = INVALID_HANDLE_VALUE;
#else
#define STREAM_TABLE "GlobalStreams"
static SDL_Thread *pending_op_thread = NULL;
static lua_State *pending_op_L = NULL;
static SDL_mutex *pending_op_mtx = NULL;
static size_t pending_op_count = 0;
static int pending_op_change[2] = { -1 };

static void set_pending_op(stream_handle_t handle, stream_t *stream) {
  if (!(stream->flags & STREAM_FLAG_NONBLOCKING)) return;
  SDL_LockMutex(pending_op_mtx);
  int exists = (lua_getglobal(pending_op_L, STREAM_TABLE), lua_rawgeti(pending_op_L, -1, handle));
  if (exists && !stream) {
    lua_pushnil(pending_op_L);
    lua_rawseti(pending_op_L, -3, handle);
    pending_op_count--;
  } else if (!exists && stream) {
    lua_pushlightuserdata(pending_op_L, stream);
    lua_rawseti(pending_op_L, -3, handle);
    pending_op_count++;
  }
  lua_pop(pending_op_L, 2);
  write(pending_op_change[1], "", 1);
  SDL_UnlockMutex(pending_op_mtx);
}
#endif

//#region Bip buffer stuff
static int buf_contiguous(const stream_buf_t *buf) { return buf->head >= buf->tail; }
static int buf_empty(const stream_buf_t *buf) { return buf->used == 0; }
static uint32_t buf_ahead(const stream_buf_t *buf) {
  return buf_contiguous(buf)
          ? buf->size - buf->head - (buf->tail == 0 ? 1 : 0)
          : buf->tail - buf->head - 1;
}
static uint32_t buf_behind(const stream_buf_t *buf) {
  return buf_contiguous(buf) ? (buf->tail == 0 ? 0 : buf->tail - 1) : 0;
}
static uint8_t *buf_reserve(stream_buf_t *buf, const uint32_t requested, uint32_t *actual) {
  const uint32_t ahead = buf_ahead(buf), behind = buf_behind(buf);
  if (ahead < requested) {
    if (behind > ahead) {
      buf->current_size = buf->head;
      buf->head = 0;
      return (*actual = SDL_min(behind, requested), buf->data);
    }
  }
  return (*actual = SDL_min(ahead, requested), buf->data + buf->head);
}
static void buf_commit(stream_buf_t *buf, const uint32_t size) {
  buf->head += SDL_min(size, buf_ahead(buf));
  buf->used += SDL_min(size, buf_ahead(buf));
}
static uint8_t *buf_get(stream_buf_t *buf, uint32_t *size) {
  return buf_contiguous(buf)
          ? (*size = buf->head - buf->tail, buf->data + buf->tail)
          : (*size = buf->current_size - buf->tail, buf->data + buf->tail);
}
static void buf_remove(stream_buf_t *buf, const uint32_t requested) {
  const uint32_t new_tail = buf->tail + requested;
  if (buf_contiguous(buf)) {
    if (new_tail < buf->head)
      buf->tail = new_tail;
    else
      buf->head = buf->tail = 0;
  } else {
    if (new_tail < buf->current_size) {
      buf->tail = new_tail;
    } else {
      buf->tail = 0;
      buf->current_size = buf->size;
    }
  }
  buf->used -= requested;
}
//#endregion

static int do_io_linux(stream_t *stream, int buf_idx, int read_op) {
  uint32_t tx_size = 0;
  uint8_t *tx_buf = read_op
                      ? buf_reserve(&stream->buf[buf_idx], stream->buf[buf_idx].pending, &tx_size)
                      : buf_get(&stream->buf[buf_idx], &tx_size);
  tx_size = SDL_min(tx_size, stream->buf[buf_idx].pending);
  if (tx_size == 0) return -100;

  ssize_t result = read_op ? read(stream->handle, tx_buf, tx_size) : write(stream->handle, tx_buf, tx_size);
  if (result == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    stream->last_error = errno;
    stream->flags |= STREAM_FLAG_EOF;
    return 0;
  }
  if (read_op)
    buf_commit(&stream->buf[buf_idx], SDL_max(0, result));
  else
    buf_remove(&stream->buf[buf_idx], SDL_max(0, result));
  stream->buf[buf_idx].pending -= SDL_max(0, result);
  stream->flags |= result == 0 ? STREAM_FLAG_EOF : 0;
  return result == -1;
}

static void do_io_task(stream_t *stream) {
  while (!(stream->flags & STREAM_FLAG_TX) && !(stream->flags & STREAM_FLAG_EOF)) {
    int pending = 0;
    if (stream->flags & STREAM_FLAG_READ)
      pending += do_io_linux(stream, 0, 1);
    if (stream->flags & STREAM_FLAG_WRITE)
      pending += do_io_linux(stream, 1, 0);
    set_pending_op(stream->handle, pending ? stream : NULL);
    if (pending < 0) break; // already flushed everything
  }
}

static int io_thread(void *userdata) {
  struct pollfd *fds = NULL;
  size_t fds_cap = 0;
  SDL_LockMutex(pending_op_mtx);
  for (;;) {
    const size_t fds_size = pending_op_count + 1;
    if (fds == NULL || fds_size > fds_cap || fds_cap >= fds_size * 2) {
      const size_t new_fds_cap = fds_size + (fds_size * 0.5f);
      struct pollfd *new_fds = realloc(fds, sizeof(struct pollfd) * new_fds_cap);
      if (!new_fds) break;
      fds = new_fds;
      fds_cap = new_fds_cap;
    }

    size_t idx = 0;
    fds[idx++] = (struct pollfd){ .fd = pending_op_change[0], .events = POLLIN };
    lua_getglobal(pending_op_L, STREAM_TABLE);
    lua_pushnil(pending_op_L);
    while (lua_next(pending_op_L, -2) != 0) {
      lua_pop(pending_op_L, 1); // discard the value, we don't need it
      fds[idx++] = (struct pollfd){.fd = lua_tointeger(pending_op_L, -1),
                                   .events = POLLIN | POLLOUT,
                                   .revents = 0};
    }
    lua_pop(pending_op_L, 1);

    // unlock and poll(), then lock to process the fds
    SDL_UnlockMutex(pending_op_mtx);
    const int result = poll(fds, idx, -1);
    SDL_LockMutex(pending_op_mtx);

    if (result == 0 || (result == -1 && errno == EINTR))
      continue;
    if (fds[0].revents & POLLIN) {
      char buf[100];
      while (read(fds[0].fd, buf, sizeof(buf)) > 0);
      continue;
    }
    if (fds[0].revents & POLLHUP)
      break;

    // process all file descriptors
    lua_getglobal(pending_op_L, STREAM_TABLE);
    for (size_t i = 1; i <= idx; i++) {
      if (fds[i].revents != 0) {
        if (lua_rawgeti(pending_op_L, -1, fds[i].fd) == LUA_TLIGHTUSERDATA)
          do_io_task(lua_touserdata(pending_op_L, -1));
        lua_pop(pending_op_L, 1);
      }
    }
    lua_pop(pending_op_L, 1);
  }
  SDL_UnlockMutex(pending_op_mtx);
  free(fds);
  return 0;
}

stream_t *lxl_stream_from_handle(lua_State *L, const stream_handle_t handle,
                                 const stream_flag_t flags,
                                 const uint32_t buf_size,
                                 const stream_finalizer_fn_t finalizer_fn,
                                 void *finalizer_userdata) {
  const uint32_t actual_buf_size = buf_size ? buf_size : DEFAULT_BUF_SIZE;
  stream_t *stream = lua_newuserdatauv(L, sizeof(stream_t), 2);
  luaL_setmetatable(L, API_TYPE_STREAM);
  memset(stream, 0, sizeof(stream_t));
  stream->mtx = SDL_CreateMutex();
  if (!stream->mtx) {
    lua_pop(L, 1);
    return NULL;
  }
  stream->handle = handle;
  stream->flags = flags;
  stream->finalizer = finalizer_fn;
  stream->finalizer_userdata = finalizer_userdata;
  for (int i = 0; i < (stream->flags & STREAM_FLAG_WRITE ? 2 : 1); i++) {
    stream->buf[i].data = lua_newuserdata(L, actual_buf_size);
    stream->buf[i].size = stream->buf[i].current_size = actual_buf_size;
    lua_setiuservalue(L, -2, i + 1);
  }
  return stream;
}

static int f_stream_read(lua_State *L) {
  lua_settop(L, 2);
  stream_t *stream = luaL_checkudata(L, 1, API_TYPE_STREAM);
  const uint32_t requested = luaL_optinteger(L, 2, UINT32_MAX);

  SDL_LockMutex(stream->mtx);
  if (!(stream->flags & STREAM_FLAG_READ)) {
    SDL_UnlockMutex(stream->mtx);
    lua_pushnil(L);
    lua_pushliteral(L, "stream does not support reading");
    return 2;
  }
  stream->buf[0].pending += requested;
  do_io_task(stream);
  if (!buf_empty(&stream->buf[0])) {
    uint32_t size;
    const uint8_t *buf = buf_get(&stream->buf[0], &size);
    const uint32_t actual = SDL_min(size, requested);
    lua_pushlstring(L, (const char *)buf, actual);
    buf_remove(&stream->buf[0], actual);
    SDL_UnlockMutex(stream->mtx);
    return 1;
  }
  if (stream->flags & STREAM_FLAG_EOF) {
    lua_pushnil(L);
    if (stream->last_error != 0)
      lua_pushstring(L, strerror(stream->last_error));
    SDL_UnlockMutex(stream->mtx);
    return stream->last_error != 0 ? 2 : 1;
  }
  // maybe we just really don't have things to read?
  lua_pushliteral(L, "");
  SDL_UnlockMutex(stream->mtx);
  return 1;
}

static int f_stream_write(lua_State *L) {
  lua_settop(L, 2);
  size_t write_size;
  stream_t *stream = luaL_checkudata(L, 1, API_TYPE_STREAM);
  const uint8_t *data = (uint8_t *)luaL_checklstring(L, 2, &write_size);

  SDL_LockMutex(stream->mtx);
  if (!(stream->flags & STREAM_FLAG_WRITE)) {
    SDL_UnlockMutex(stream->mtx);
    lua_pushnil(L);
    lua_pushliteral(L, "writing is not supported for this stream");
    return 2;
  }
  if (stream->flags & STREAM_FLAG_EOF) {
    lua_pushnil(L);
    if (stream->last_error != 0)
      lua_pushstring(L, strerror(stream->last_error));
    SDL_UnlockMutex(stream->mtx);
    return stream->last_error != 0 ? 2 : 1;
  }
  uint32_t size;
  uint8_t *buf = buf_reserve(&stream->buf[1], write_size, &size);
  memcpy(buf, data, size);
  buf_commit(&stream->buf[1], size);
  stream->buf[1].pending += size;
  do_io_task(stream);
  SDL_UnlockMutex(stream->mtx);
  lua_pushinteger(L, size);
  return 1;
}

static int f_stream_get_properties(lua_State *L) {
  lua_settop(L, 1);
  const stream_t *stream = luaL_checkudata(L, 1, API_TYPE_STREAM);
  SDL_LockMutex(stream->mtx);
  lua_newtable(L);
  lua_pushboolean(L, !!(stream->flags & STREAM_FLAG_EOF));
  lua_setfield(L, -2, "eof");
  lua_pushboolean(L, !!(stream->flags & STREAM_FLAG_NONBLOCKING));
  lua_setfield(L, -2, "nonblocking");
  lua_pushstring(L, stream->flags & STREAM_FLAG_READ ? "read" : "write");
  lua_setfield(L, -2, "mode");
  lua_newtable(L);
  for (int i = 0; i < (stream->flags & STREAM_FLAG_WRITE ? 2 : 1); i++) {
    lua_newtable(L);
    lua_pushinteger(L, stream->buf[i].size);
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, stream->buf[i].used);
    lua_setfield(L, -2, "used");
    lua_pushinteger(L, stream->buf[i].pending);
    lua_setfield(L, -2, "pending");
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, -2, "buffers");
  SDL_UnlockMutex(stream->mtx);
  return 1;
}

static int f_stream_close(lua_State *L) {
  lua_settop(L, 1);
  stream_t *stream = luaL_checkudata(L, 1, API_TYPE_STREAM);
  SDL_LockMutex(stream->mtx);
  if (stream->handle != INVALID_STREAM_HANDLE) {
    if (stream->flags & STREAM_FLAG_NONBLOCKING) {
      SDL_LockMutex(pending_op_mtx);
      lua_getglobal(pending_op_L, STREAM_TABLE);
      lua_pushnil(pending_op_L);
      lua_rawseti(pending_op_L, -2, stream->handle);
      lua_pop(pending_op_L, 1);
      SDL_UnlockMutex(pending_op_mtx);
    }
    if (stream->finalizer)
      stream->finalizer(stream, stream->finalizer_userdata);
    stream->handle = INVALID_STREAM_HANDLE;
  }
  SDL_UnlockMutex(stream->mtx);
  return 0;
}

static int f_stream_gc(lua_State *L) {
  f_stream_close(L);
  const stream_t *stream = luaL_checkudata(L, 1, API_TYPE_STREAM);
  SDL_DestroyMutex(stream->mtx);
  return 0;
}

static int f_stream_thread_gc(lua_State *L) {
  close(pending_op_change[1]);
  SDL_WaitThread(pending_op_thread, NULL);
  SDL_DestroyMutex(pending_op_mtx);
  lua_close(pending_op_L);
  return 0;
}

static const luaL_Reg stream_lib[] = {
  {"read", f_stream_read},
  {"write", f_stream_write},
  {"get_properties", f_stream_get_properties},
  {"close", f_stream_close},
  {"__gc", f_stream_gc},
  {"__close", f_stream_close},
  {NULL, NULL},
};

static int set_nonblock(int fd, int extra_flags) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) return -1;
  if (fcntl(fd, F_SETFL, flags | extra_flags) != 0) return -1;
  return 0;
}

const char *const socket_type[] = { "tcp", "udp", NULL };

static int f_socket_create(lua_State *L) {
  lua_settop(L, 1);
  const int type = luaL_checkoption(L, 1, "tcp", socket_type);
  const int sockfd = socket(AF_INET, type == 0 ? SOCK_STREAM : SOCK_DGRAM, 0);
  if (sockfd == -1)
    return luaL_error(L, "socket(): %s", strerror(sockfd));

  if (set_nonblock(sockfd, O_NONBLOCK)) {
    close(sockfd);
    return luaL_error(L, "fcntl(listenfd, F_SETFL, O_NONBLOCK): %s", strerror(errno));
  }

  int *sock = lua_newuserdata(L, sizeof(int));
  luaL_setmetatable(L, API_TYPE_SOCKET);
  *sock = sockfd;
  return 1;
}

static int f_socket_listen(lua_State *L) {
  lua_settop(L, 4);
  int *sock = luaL_checkudata(L, 1, API_TYPE_SOCKET);
  const char *ip = luaL_checkstring(L, 2);
  const int port = luaL_checkinteger(L, 3);
  const int backlog = luaL_optinteger(L, 4, 50);

  struct sockaddr_in addr = { 0 };
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
    return luaL_error(L, "inet_pton(): %s", strerror(errno));

  if (bind(*sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
    return luaL_error(L, "bind(): %s", strerror(errno));

  if (listen(*sock, backlog) != 0)
    return luaL_error(L, "listen(): %s", strerror(errno));

  return 0;
}

static void sock_finalizer(stream_t *stream, void *userdata) {
  close(stream->handle);
}

static int f_socket_accept(lua_State *L) {
  lua_settop(L, 1);
  int *sock = luaL_checkudata(L, 1, API_TYPE_SOCKET);
  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(addr);
  int acceptfd = accept(*sock, (struct sockaddr *) &addr, &addr_size);
  if (acceptfd != 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  } else if (acceptfd != -1) {
    if (set_nonblock(acceptfd, O_NONBLOCK) != 0) {
      close(acceptfd);
      lua_pushnil(L);
      lua_pushstring(L, strerror(errno));
      return 2;
    }
    lxl_stream_from_handle(L, acceptfd, STREAM_FLAG_READ | STREAM_FLAG_WRITE | STREAM_FLAG_NONBLOCKING, 0, sock_finalizer, NULL);
    return 1;
  }
  return 0;
}

static int f_socket_close(lua_State *L) {
  int *sock = luaL_checkudata(L, 1, API_TYPE_SOCKET);
  close(*sock);
  *sock = -1;
  return 0;
}

static const luaL_Reg socket_lib[] = {
  { "create", f_socket_create },
  { "listen", f_socket_listen },
  { "accept", f_socket_accept },
  { "__close", f_socket_close },
  { "__gc", f_socket_close },
  { NULL, NULL },
};

int luaopen_stream(lua_State *L) {
#ifndef _WIN32
  if (pipe(pending_op_change) != 0)
    return luaL_error(L, "pipe() failed: %s", strerror(errno));

  set_nonblock(pending_op_change[0], O_NONBLOCK);
  set_nonblock(pending_op_change[1], O_NONBLOCK);

  pending_op_L = luaL_newstate();
  if (!pending_op_L)
    return luaL_error(L, "cannot create lua_State for IO operations");
  lua_newtable(pending_op_L);
  lua_setglobal(pending_op_L, STREAM_TABLE);

  pending_op_mtx = SDL_CreateMutex();
  if (!pending_op_mtx)
    return luaL_error(L, "Failed to create mutex");

  pending_op_thread = SDL_CreateThread(io_thread, "IO thread", NULL);
  if (!pending_op_thread)
    return luaL_error(L, "failed to start IO thread");

#endif

  // stream thread finalizer
  lua_newtable(L);
  lua_pushcfunction(L, f_stream_thread_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushvalue(L, -1);
  lua_setmetatable(L, -2);
  lua_setfield(L, LUA_REGISTRYINDEX, STREAM_TABLE);

  lua_newtable(L);

  // stream API
  luaL_newmetatable(L, API_TYPE_STREAM);
  luaL_setfuncs(L, stream_lib, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_setfield(L, -2, "stream");

  // socket API
  luaL_newmetatable(L, API_TYPE_SOCKET);
  luaL_setfuncs(L, socket_lib, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_setfield(L, -2, "socket");

  return 1;
}
