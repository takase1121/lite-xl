#ifndef STREAM_H
#define STREAM_H

#include <lua.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE stream_handle_t;
#define INVALID_STREAM_HANDLE INVALID_HANDLE_VALUE
#else
typedef int stream_handle_t;
#define INVALID_STREAM_HANDLE ((stream_handle_t)-1)
#endif

typedef enum {
  STREAM_FLAG_READ = (1 << 0),        // stream supports read operation
  STREAM_FLAG_WRITE = (1 << 1),       // stream supports write operation
  STREAM_FLAG_NONBLOCKING = (1 << 2), // stream supports non-blocking IO
  STREAM_FLAG_EOF = (1 << 3),         // stream has reached EOF / errored
  STREAM_FLAG_TX = (1 << 4),          // stream is doing IO (mainly used on Windows)
} stream_flag_t;

typedef struct stream_s stream_t;
typedef void (*stream_finalizer_fn_t)(stream_t* stream, void* userdata);

stream_t *lxl_stream_from_handle(lua_State *L,
                                 const stream_handle_t handle,
                                 const stream_flag_t flags,
                                 const uint32_t buf_size,
                                 const stream_finalizer_fn_t finalizer_fn,
                                 void *finalizer_userdata);

#endif //STREAM_H
