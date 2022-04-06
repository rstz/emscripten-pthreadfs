#ifndef PTHREADFS_H
#define PTHREADFS_H

#include <assert.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <pthread.h>

#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <wasi/api.h>

// The following macros convert the PTHREADFS_FOLDER to a string that can be used by C++
#define QUOTE(name) #name
#define STR(macro) QUOTE(macro)

#ifndef PTHREADFS_FOLDER
#define PTHREADFS_FOLDER persistent
#endif // PTHREADFS_FOLDER
#define PTHREADFS_FOLDER_NAME STR(PTHREADFS_FOLDER)

// Emscripten changed the names of syscalls with version 2.0.31.
// These macros translate between the old and new names
#if __EMSCRIPTEN_major__ > 2 || (__EMSCRIPTEN_major__==2 && __EMSCRIPTEN_tiny__ > 31)
#define SYS_CAPI_DEF(name, number, ...) long __syscall_##name(__VA_ARGS__)
#define SYNC_JS_SYSCALL(name) __env_syscall_##name
#define SYS_JS_DEF(name, ...)                                                                     \
EM_IMPORT(__syscall_##name)  long SYNC_JS_SYSCALL(name)(__VA_ARGS__);
#else  // __EMSCRIPTEN_major__ > 2 || (__EMSCRIPTEN_major__==2 && __EMSCRIPTEN_tiny__ > 31)
#define SYS_CAPI_DEF(name, number, ...) long __syscall##number(__VA_ARGS__)
#define SYNC_JS_SYSCALL(name) __sys_##name
#define SYS_JS_DEF(name, ...) extern long SYNC_JS_SYSCALL(name)(__VA_ARGS__);
#endif  // __EMSCRIPTEN_major__ > 2 || (__EMSCRIPTEN_major__==2 && __EMSCRIPTEN_tiny__ > 31)



// clang-format will incorrectly transform the Javascript code.
// clang-format off
#define EM_PTHREADFS_ASM(code)                                                                     \
  g_sync_to_async_helper.invoke([](emscripten::sync_to_async::Callback resume) {                   \
    g_resumeFct = [resume]() { (*resume)(); };                                                     \
    EM_ASM({ (async() => { code wasmTable.get($0)(); })(); }, &resumeWrapper_v);                   \
  });
// clang-format on

#define WASI_JSAPI_DEF(name, ...)                                                                  \
  extern void __fd_##name##_async(__wasi_fd_t fd, __VA_ARGS__, void (*fun)(__wasi_errno_t));       \
  __attribute__((__import_module__("wasi_snapshot_preview1"), __import_name__(QUOTE(fd_##name))))  \
  EM_IMPORT(fd_##name) __wasi_errno_t fd_##name(__wasi_fd_t fd, __VA_ARGS__);
#define WASI_JSAPI_NOARGS_DEF(name)                                                                \
  extern void __fd_##name##_async(__wasi_fd_t fd, void (*fun)(__wasi_errno_t));                    \
  __attribute__((__import_module__("wasi_snapshot_preview1"), __import_name__(QUOTE(fd_##name))))  \
  EM_IMPORT(fd_##name) __wasi_errno_t fd_##name(__wasi_fd_t fd);

#define WASI_CAPI_DEF(name, ...) __wasi_errno_t __wasi_fd_##name(__wasi_fd_t fd, __VA_ARGS__)
#define WASI_CAPI_NOARGS_DEF(name) __wasi_errno_t __wasi_fd_##name(__wasi_fd_t fd)

#define WASI_SYNC_TO_ASYNC(name, ...)                                                              \
  if (fsa_file_descriptors.count(fd) > 0) {                                                        \
    g_sync_to_async_helper.invoke([fd, __VA_ARGS__](emscripten::sync_to_async::Callback resume) {  \
      g_resumeFct = [resume]() { (*resume)(); };                                                   \
      __fd_##name##_async(fd, __VA_ARGS__, &resumeWrapper_wasi);                                   \
    });                                                                                            \
    return resume_result_wasi;                                                                     \
  }                                                                                                \
  return fd_##name(fd, __VA_ARGS__);
#define WASI_SYNC_TO_ASYNC_NOARGS(name)                                                            \
  if (fsa_file_descriptors.count(fd) > 0) {                                                        \
    g_sync_to_async_helper.invoke([fd](emscripten::sync_to_async::Callback resume) {               \
      g_resumeFct = [resume]() { (*resume)(); };                                                   \
      __fd_##name##_async(fd, &resumeWrapper_wasi);                                                \
    });                                                                                            \
    return resume_result_wasi;                                                                     \
  }                                                                                                \
  return fd_##name(fd);

// Classic Syscalls
#define SYS_JSAPI_DEF(name, ...)                                                                   \
  extern void __sys_##name##_async(__VA_ARGS__, void (*fun)(long));                                \
  SYS_JS_DEF(name, __VA_ARGS__);

#define SYS_JSAPI_NOARGS_DEF(name)                                                                 \
  extern void __sys_##name##_async(void (*fun)(long));                                             \
  extern long SYNC_JS_SYSCALL(name)();

#define SYS_DEF(name, number, ...)                                                                 \
  SYS_CAPI_DEF(name, number, __VA_ARGS__);                                                         \
  SYS_JSAPI_DEF(name, __VA_ARGS__)

#define SYS_JSAPI(name, ...) __sys_##name##_async(__VA_ARGS__)
#define SYS_SYNC_TO_ASYNC_NORETURN(name, ...)                                                      \
  g_sync_to_async_helper.invoke([__VA_ARGS__](emscripten::sync_to_async::Callback resume) {        \
    g_resumeFct = [resume]() { (*resume)(); };                                                     \
    SYS_JSAPI(name, __VA_ARGS__, &resumeWrapper_l);                                                \
  });
#define SYS_SYNC_TO_ASYNC_FD(name, ...)                                                            \
  if (fsa_file_descriptors.count(fd) > 0) {                                                        \
    g_sync_to_async_helper.invoke([__VA_ARGS__](emscripten::sync_to_async::Callback resume) {      \
      g_resumeFct = [resume]() { (*resume)(); };                                                   \
      __sys_##name##_async(__VA_ARGS__, &resumeWrapper_l);                                         \
    });                                                                                            \
    return resume_result_long;                                                                     \
  }                                                                                                \
  return SYNC_JS_SYSCALL(name)(__VA_ARGS__);
#define SYS_SYNC_TO_ASYNC_PATH(name, ...)                                                          \
  std::string pathname((char*)path);                                                               \
  if (emscripten::is_pthreadfs_file(pathname)) {                                                   \
    g_sync_to_async_helper.invoke([__VA_ARGS__](emscripten::sync_to_async::Callback resume) {      \
      g_resumeFct = [resume]() { (*resume)(); };                                                   \
      __sys_##name##_async(__VA_ARGS__, &resumeWrapper_l);                                         \
    });                                                                                            \
    return resume_result_long;                                                                     \
  }                                                                                                \
  return SYNC_JS_SYSCALL(name)(__VA_ARGS__);

extern "C" {
// Helpers
extern void pthreadfs_init(const char* folder, void (*fun)(void));
void pthreadfs_load_package(const char* path_to_package);
void emscripten_init_pthreadfs();

// WASI
WASI_JSAPI_DEF(write, const __wasi_ciovec_t* iovs, size_t iovs_len, __wasi_size_t* nwritten)
WASI_JSAPI_DEF(read, const __wasi_iovec_t* iovs, size_t iovs_len, __wasi_size_t* nread)
WASI_JSAPI_DEF(pwrite, const __wasi_ciovec_t* iovs, size_t iovs_len, __wasi_filesize_t offset,
  __wasi_size_t* nwritten)
WASI_JSAPI_DEF(pread, const __wasi_iovec_t* iovs, size_t iovs_len, __wasi_filesize_t offset,
  __wasi_size_t* nread)
WASI_JSAPI_DEF(
  seek, __wasi_filedelta_t offset, __wasi_whence_t whence, __wasi_filesize_t* newoffset)
WASI_JSAPI_DEF(fdstat_get, __wasi_fdstat_t* stat)
WASI_JSAPI_NOARGS_DEF(close)
WASI_JSAPI_NOARGS_DEF(sync)

// Syscalls
// see
// https://github.com/emscripten-core/emscripten/blob/main/system/lib/libc/musl/arch/emscripten/syscall_arch.h
SYS_CAPI_DEF(open, 5, long path, long flags, ...);
SYS_JSAPI_DEF(open, long path, long flags, int varargs)

SYS_CAPI_DEF(unlink, 10, long path);
SYS_JSAPI_DEF(unlink, long path)

SYS_CAPI_DEF(chdir, 12, long path);
SYS_JSAPI_DEF(chdir, long path)

SYS_CAPI_DEF(mknod, 14, long path, long mode, long dev);
SYS_JSAPI_DEF(mknod, long path, long mode, long dev)

SYS_CAPI_DEF(chmod, 15, long path, long mode);
SYS_JSAPI_DEF(chmod, long path, long mode)

SYS_CAPI_DEF(access, 33, long path, long amode);
SYS_JSAPI_DEF(access, long path, long amode)

SYS_CAPI_DEF(rename, 38, long old_path, long new_path);
SYS_JSAPI_DEF(rename, long old_path, long new_path)

SYS_CAPI_DEF(mkdir, 39, long path, long mode);
SYS_JSAPI_DEF(mkdir, long path, long mode)

SYS_CAPI_DEF(rmdir, 40, long path);
SYS_JSAPI_DEF(rmdir, long path)

SYS_CAPI_DEF(ioctl, 54, long fd, long request, ...);
SYS_JSAPI_DEF(ioctl, long fd, long request, void* const varargs)

SYS_CAPI_DEF(readlink, 85, long path, long buf, long bufsize);
SYS_JSAPI_DEF(readlink, long path, long buf, long bufsize)

SYS_CAPI_DEF(fchmod, 94, long fd, long mode);
SYS_JSAPI_DEF(fchmod, long fd, long mode)

SYS_CAPI_DEF(fchdir, 133, long fd);
SYS_JSAPI_DEF(fchdir, long fd)

SYS_CAPI_DEF(fdatasync, 148, long fd);
SYS_JSAPI_DEF(fdatasync, long fd)

SYS_CAPI_DEF(truncate64, 193, long path, uint64_t length);
SYS_JSAPI_DEF(truncate64, long path, uint64_t length)

SYS_CAPI_DEF(ftruncate64, 194, long fd, uint64_t length);
SYS_JSAPI_DEF(ftruncate64, long fd, uint64_t length)

SYS_CAPI_DEF(stat64, 195, long path, long buf);
SYS_JSAPI_DEF(stat64, long path, long buf)

SYS_CAPI_DEF(lstat64, 196, long path, long buf);
SYS_JSAPI_DEF(lstat64, long path, long buf)

SYS_CAPI_DEF(fstat64, 197, long fd, long buf);
SYS_JSAPI_DEF(fstat64, long fd, long buf)

SYS_CAPI_DEF(lchown32, 198, long path, long owner, long group);
SYS_JSAPI_DEF(lchown32, long path, long owner, long group)

SYS_CAPI_DEF(fchown32, 207, long fd, long owner, long group);
SYS_JSAPI_DEF(fchown32, long fd, long owner, long group)

SYS_CAPI_DEF(chown32, 212, long path, long owner, long group);
SYS_JSAPI_DEF(chown32, long path, long owner, long group)

SYS_CAPI_DEF(getdents64, 220, long fd, long dirp, long count);
SYS_JSAPI_DEF(getdents64, long fd, long dirp, long count)

SYS_CAPI_DEF(fcntl64, 221, long fd, long cmd, ...);
SYS_JSAPI_DEF(fcntl64, long fd, long cmd, int varargs)

SYS_CAPI_DEF(statfs64, 268, long path, long size, long buf);
SYS_JSAPI_DEF(statfs64, long path, long size, long buf)

SYS_CAPI_DEF(fstatfs64, 269, long fd, long size, long buf);
SYS_JSAPI_DEF(fstatfs64, long fd, long size, long buf)

SYS_CAPI_DEF(
  fallocate, 324, long fd, long mode, long offset, long length);
SYS_JSAPI_DEF(
  fallocate, long fd, long mode, long offset, long length)

// Emscripten implements utime directly through library.js. We copy that code 
// to utime_sync in order to avoid name confusion. utime_async proxies the
// calls to the IO thread.
extern long utime_sync(long path_ref, long times);
extern void utime_async(long path_ref, long times, void (*fun)(long));
long utime(long path_ref, long times);
} // extern "C"

namespace emscripten {

// Helper class for generic sync-to-async conversion. Creating an instance of
// this class will spin up a pthread. You can then call invoke() to run code
// on that pthread. The work done on the pthread receives a callback method
// which lets you indicate when it finished working. The call to invoke() is
// synchronous, while the work done on the other thread can be asynchronous,
// which allows bridging async JS APIs to sync C++ code.
//
// This can be useful if you are in a location where blocking is possible (like
// a thread, or when using PROXY_TO_PTHREAD), but you have code that is hard to
// refactor to be async, but that requires some async operation (like waiting
// for a JS event).
class sync_to_async {
public:
  // Pass around the callback as a pointer to a std::function. Using a pointer
  // means that it can be sent easily to JS, as a void* parameter to a C API,
  // etc., and also means we do not need to worry about the lifetime of the
  // std::function in user code.
  using Callback = std::function<void()>*;

  sync_to_async();

  ~sync_to_async();

  // Run some work on thread. This is a synchronous (blocking) call. The thread
  // where the work actually runs can do async work for us - all it needs to do
  // is call the given callback function when it is done.
  //
  // Note that you need to call the callback even if you are not async, as the
  // code here does not know if you are async or not. For example,
  //
  //  instance.invoke([](emscripten::sync_to_async::Callback resume) {
  //    std::cout << "Hello from sync C++ on the pthread\n";
  //    (*resume)();
  //  });
  //
  // In the async case, you would call resume() at some later time.
  //
  // It is safe to call this method from multiple threads, as it locks itself.
  // That is, you can create an instance of this and call it from multiple
  // threads freely.
  void invoke(std::function<void(Callback)> newWork);

  //==============================================================================
  // End Public API

private:
  std::unique_ptr<std::thread> thread;
  std::mutex mutex;
  std::mutex invokeMutex;
  std::condition_variable condition;
  std::function<void(Callback)> work;
  std::unique_ptr<std::function<void()>> resume;

  bool readyToWork = false;
  bool finishedWork;
  bool quit = false;

  bool pthreadfs_initialized = false;

  // The child will be asynchronous, and therefore we cannot rely on RAII to
  // unlock for us, we must do it manually.
  std::unique_lock<std::mutex> childLock;

  static void* threadMain(void* arg);

  static void threadIter(void* arg);
};

// Determines if `path` is a file in the special folder PTHREADFS_FOLDER.
bool is_pthreadfs_file(std::string path);
// Determines is `path` is a symlink in self/proc/fd/ that corresponds to a file in
// PTHREADFS_FOLDER.
bool is_pthreadfs_fd_link(std::string path);

} // namespace emscripten

// Declare global variables to be populated by resume;
extern std::function<void()> g_resumeFct;
extern emscripten::sync_to_async g_sync_to_async_helper;

// Static functions calling resumFct and setting corresponding the return value.
void resumeWrapper_v();

void resumeWrapper_l(long retVal);

void resumeWrapper_wasi(__wasi_errno_t retVal);

#endif // PTHREADFS_H
