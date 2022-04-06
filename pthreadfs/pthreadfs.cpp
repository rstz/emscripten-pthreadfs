#include "pthreadfs.h"

#include <assert.h>
#include <emscripten.h>
#include <pthread.h>
#include <sys/stat.h>
#include <wasi/api.h>

#include <functional>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <utility>

#include <stdarg.h>

// The following uses Emscripten's Threadutil, see
// https://github.com/emscripten-core/emscripten/pull/14666 for details.
namespace emscripten {

sync_to_async::sync_to_async() : childLock(mutex) {
  // The child lock is associated with the mutex, which takes the lock as we
  // connect them, and so we must free it here so that the child can use it.
  // Only the child will lock/unlock it from now on.
  childLock.unlock();

  // Create the thread after the lock is ready.
  thread = std::make_unique<std::thread>(threadMain, this);
}

sync_to_async::~sync_to_async() {
  // Wake up the child to tell it to quit.
  invoke([&](Callback func) {
    quit = true;
    (*func)();
  });

  thread->join();
}

void sync_to_async::invoke(std::function<void(sync_to_async::Callback)> newWork) {
  // Use the invokeMutex to prevent more than one invoke being in flight at a
  // time, so that this is usable from multiple threads safely.
  std::lock_guard<std::mutex> invokeLock(invokeMutex);
  // Initialize the PThreadFS file system.
  if (!pthreadfs_initialized) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      work = [](sync_to_async::Callback done) {
        g_resumeFct = [done]() { (*done)(); };
        pthreadfs_init(PTHREADFS_FOLDER_NAME, &resumeWrapper_v);
      };
      finishedWork = false;
      readyToWork = true;
    }
    condition.notify_one();

    // Wait for it to be complete.
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [&]() { return finishedWork; });
    pthreadfs_initialized = true;
  }
  // Send the work over.
  {
    std::lock_guard<std::mutex> lock(mutex);
    work = newWork;
    finishedWork = false;
    readyToWork = true;
  }
  condition.notify_one();

  // Wait for it to be complete.
  std::unique_lock<std::mutex> lock(mutex);
  condition.wait(lock, [&]() { return finishedWork; });
}

void* sync_to_async::threadMain(void* arg) {
  // Prevent the pthread from shutting down too early.
  EM_ASM(runtimeKeepalivePush(););
  emscripten_async_call(threadIter, arg, 0);
  return 0;
}

void sync_to_async::threadIter(void* arg) {
  auto* parent = (sync_to_async*)arg;
  if (parent->quit) {
    EM_ASM(runtimeKeepalivePop(););
    pthread_exit(0);
  }
  // Wait until we get something to do.
  parent->childLock.lock();
  parent->condition.wait(parent->childLock, [&]() { return parent->readyToWork; });
  auto work = parent->work;
  parent->readyToWork = false;
  // Allocate a resume function, and stash it on the parent.
  parent->resume = std::make_unique<std::function<void()>>([parent, arg]() {
    // We are called, so the work was finished. Notify the caller.
    parent->finishedWork = true;
    parent->childLock.unlock();
    parent->condition.notify_one();
    // Look for more work.
    // For performance reasons, this is an asynchronous call. If a synchronous API
    // were to be called, these chained calls would lead to an out-of-stack error.
    threadIter(arg);
  });
  // Run the work function the user gave us. Give it a pointer to the resume
  // function.
  work(parent->resume.get());
}

bool is_pthreadfs_file(std::string path) {
  auto const regex = std::regex("/*" PTHREADFS_FOLDER_NAME "(/*$|/+.*)");
  return std::regex_match(path, regex);
}

bool is_pthreadfs_fd_link(std::string path) {
  auto const regex = std::regex("^/*proc/+self/+fd/+([0-9]+)$");
  std::smatch match;
  if (regex_match(path, match, regex)){
    char* p;
    long fd = strtol(match.str(1).c_str(), &p, 10);
    // As defined in library_asyncfs.js, the minimum fd for PThreadFS is 4097.
    if (*p == 0 && fd >= 4097) {
      return true;
    }
  }
  return false;
}

} // namespace emscripten

// Static functions calling resumFct and setting the return value.
void resumeWrapper_v() { g_resumeFct(); }
// return value long
long resume_result_long = 0;
void resumeWrapper_l(long retVal) {
  resume_result_long = retVal;
  g_resumeFct();
}
// return value __wasi_errno_t
__wasi_errno_t resume_result_wasi = 0;
void resumeWrapper_wasi(__wasi_errno_t retVal) {
  resume_result_wasi = retVal;
  g_resumeFct();
}

// File System Access collection
std::set<long> fsa_file_descriptors;
std::set<std::string> mounted_directories;

// Wasi definitions
WASI_CAPI_DEF(write, const __wasi_ciovec_t* iovs, size_t iovs_len, __wasi_size_t* nwritten) {
  WASI_SYNC_TO_ASYNC(write, iovs, iovs_len, nwritten);
}

WASI_CAPI_DEF(read, const __wasi_iovec_t* iovs, size_t iovs_len, __wasi_size_t* nread) {
  WASI_SYNC_TO_ASYNC(read, iovs, iovs_len, nread);
}
WASI_CAPI_DEF(pwrite, const __wasi_ciovec_t* iovs, size_t iovs_len, __wasi_filesize_t offset,
  __wasi_size_t* nwritten) {
  WASI_SYNC_TO_ASYNC(pwrite, iovs, iovs_len, offset, nwritten);
}
WASI_CAPI_DEF(pread, const __wasi_iovec_t* iovs, size_t iovs_len, __wasi_filesize_t offset,
  __wasi_size_t* nread) {
  WASI_SYNC_TO_ASYNC(pread, iovs, iovs_len, offset, nread);
}
WASI_CAPI_DEF(
  seek, __wasi_filedelta_t offset, __wasi_whence_t whence, __wasi_filesize_t* newoffset) {
  WASI_SYNC_TO_ASYNC(seek, offset, whence, newoffset);
}
WASI_CAPI_DEF(fdstat_get, __wasi_fdstat_t* stat) { WASI_SYNC_TO_ASYNC(fdstat_get, stat); }
WASI_CAPI_NOARGS_DEF(close) {
  if (fsa_file_descriptors.count(fd) > 0) {
    g_sync_to_async_helper.invoke([fd](emscripten::sync_to_async::Callback resume) {
      g_resumeFct = [resume]() { (*resume)(); };
      __fd_close_async(fd, &resumeWrapper_wasi);
    });
    if (resume_result_wasi == __WASI_ERRNO_SUCCESS) {
      fsa_file_descriptors.erase(fd);
    }
    return resume_result_wasi;
  }
  return fd_close(fd);
}
WASI_CAPI_NOARGS_DEF(sync) { WASI_SYNC_TO_ASYNC_NOARGS(sync); }

// Syscall definitions
SYS_CAPI_DEF(open, 5, long path_ref, long flags, ...) {

  std::string path((char*)path_ref);
  if (emscripten::is_pthreadfs_file(path)) {
    va_list vl;
    va_start(vl, flags);
    mode_t mode = va_arg(vl, mode_t);
    va_end(vl);
    SYS_SYNC_TO_ASYNC_NORETURN(open, path_ref, flags, mode);
    fsa_file_descriptors.insert(resume_result_long);
    return resume_result_long;
  }
  va_list vl;
  va_start(vl, flags);
  long res = SYNC_JS_SYSCALL(open)(path_ref, flags, (int)vl);
  va_end(vl);
  return res;
}

SYS_CAPI_DEF(unlink, 10, long path) { SYS_SYNC_TO_ASYNC_PATH(unlink, path); }

SYS_CAPI_DEF(chdir, 12, long path) { SYS_SYNC_TO_ASYNC_PATH(chdir, path); }

SYS_CAPI_DEF(mknod, 14, long path, long mode, long dev) {
  SYS_SYNC_TO_ASYNC_PATH(mknod, path, mode, dev);
}

SYS_CAPI_DEF(chmod, 15, long path, long mode) { SYS_SYNC_TO_ASYNC_PATH(chmod, path, mode); }

SYS_CAPI_DEF(access, 33, long path, long amode) { SYS_SYNC_TO_ASYNC_PATH(access, path, amode); }

SYS_CAPI_DEF(rename, 38, long old_path_ref, long new_path_ref) {
  std::string old_path((char*)old_path_ref);
  std::string new_path((char*)new_path_ref);

  if (emscripten::is_pthreadfs_file(old_path)) {
    if (emscripten::is_pthreadfs_file(new_path)) {
      SYS_SYNC_TO_ASYNC_NORETURN(rename, old_path_ref, new_path_ref);
      return resume_result_long;
    }
    return EXDEV;
  }
  if (emscripten::is_pthreadfs_file(new_path)) {
    return EXDEV;
  }
  long res = SYNC_JS_SYSCALL(rename)(old_path_ref, new_path_ref);
  return res;
}

SYS_CAPI_DEF(mkdir, 39, long path, long mode) { SYS_SYNC_TO_ASYNC_PATH(mkdir, path, mode); }

SYS_CAPI_DEF(rmdir, 40, long path) { SYS_SYNC_TO_ASYNC_PATH(rmdir, path); }

SYS_CAPI_DEF(ioctl, 54, long fd, long request, ...) {
  void* arg;
  va_list ap;
  va_start(ap, request);
  arg = va_arg(ap, void*);
  va_end(ap);

  SYS_SYNC_TO_ASYNC_FD(ioctl, fd, request, arg);
}

// The implementation of readlink includes special handling for the file descriptor's symlinks in
// /proc/self/fd/. This is necessary for handling realpath.
SYS_CAPI_DEF(readlink, 85, long path, long buf, long bufsize) {
  std::string pathname((char*)path);
  if (emscripten::is_pthreadfs_file(pathname) || emscripten::is_pthreadfs_fd_link(pathname)) {
    SYS_SYNC_TO_ASYNC_NORETURN(readlink, path, buf, bufsize);
    return resume_result_long;
  }
  return SYNC_JS_SYSCALL(readlink)(path, buf, bufsize);
}

SYS_CAPI_DEF(fchmod, 94, long fd, long mode) { SYS_SYNC_TO_ASYNC_FD(fchmod, fd, mode); }

SYS_CAPI_DEF(fchdir, 133, long fd) { SYS_SYNC_TO_ASYNC_FD(fchdir, fd); }

SYS_CAPI_DEF(fdatasync, 148, long fd) { SYS_SYNC_TO_ASYNC_FD(fdatasync, fd); }

SYS_CAPI_DEF(truncate64, 193, long path, uint64_t length) {
  SYS_SYNC_TO_ASYNC_PATH(truncate64, path, length);
}

SYS_CAPI_DEF(ftruncate64, 194, long fd, uint64_t length) {
  SYS_SYNC_TO_ASYNC_FD(ftruncate64, fd, length);
}

SYS_CAPI_DEF(stat64, 195, long path, long buf) {
  std::string pathname((char*)path);
  if (emscripten::is_pthreadfs_file(pathname)) {
    g_sync_to_async_helper.invoke([path, buf](emscripten::sync_to_async::Callback resume) {
      g_resumeFct = [resume]() { (*resume)(); };
      __sys_stat64_async(path, buf, &resumeWrapper_l);
    });
    return resume_result_long;
  }
  return SYNC_JS_SYSCALL(stat64)(path, buf);
}

SYS_CAPI_DEF(lstat64, 196, long path, long buf) {
  std::string pathname((char*)path);
  if (emscripten::is_pthreadfs_file(pathname)) {
    g_sync_to_async_helper.invoke([path, buf](emscripten::sync_to_async::Callback resume) {
      g_resumeFct = [resume]() { (*resume)(); };
      __sys_lstat64_async(path, buf, &resumeWrapper_l);
    });
    return resume_result_long;
  }
  return SYNC_JS_SYSCALL(lstat64)(path, buf);
}

SYS_CAPI_DEF(fstat64, 197, long fd, long buf) {
  if (fsa_file_descriptors.count(fd) > 0) {
    g_sync_to_async_helper.invoke([fd, buf](emscripten::sync_to_async::Callback resume) {
      g_resumeFct = [resume]() { (*resume)(); };
      __sys_fstat64_async(fd, buf, &resumeWrapper_l);
    });
    return resume_result_long;
  }
  return SYNC_JS_SYSCALL(fstat64)(fd, buf);
}

SYS_CAPI_DEF(lchown32, 198, long path, long owner, long group) {
  SYS_SYNC_TO_ASYNC_PATH(lchown32, path, owner, group);
}

SYS_CAPI_DEF(fchown32, 207, long fd, long owner, long group) {
  SYS_SYNC_TO_ASYNC_FD(fchown32, fd, owner, group);
}

SYS_CAPI_DEF(chown32, 212, long path, long owner, long group) {
  SYS_SYNC_TO_ASYNC_PATH(chown32, path, owner, group);
}

SYS_CAPI_DEF(getdents64, 220, long fd, long dirp, long count) {
  SYS_SYNC_TO_ASYNC_FD(getdents64, fd, dirp, count);
}

SYS_CAPI_DEF(fcntl64, 221, long fd, long cmd, ...) {

  if (fsa_file_descriptors.count(fd) > 0) {
    // varargs are currently unused by __sys_fcntl64_async.
    va_list vl;
    va_start(vl, cmd);
    int varargs = va_arg(vl, int);
    va_end(vl);
    g_sync_to_async_helper.invoke([fd, cmd, varargs](emscripten::sync_to_async::Callback resume) {
      g_resumeFct = [resume]() { (*resume)(); };
      __sys_fcntl64_async(fd, cmd, varargs, &resumeWrapper_l);
    });
    return resume_result_long;
  }
  va_list vl;
  va_start(vl, cmd);
  long res = SYNC_JS_SYSCALL(fcntl64)(fd, cmd, (int)vl);
  va_end(vl);
  return res;
}

SYS_CAPI_DEF(statfs64, 268, long path, long size, long buf) {
  SYS_SYNC_TO_ASYNC_PATH(statfs64, path, size, buf);
}

SYS_CAPI_DEF(fstatfs64, 269, long fd, long size, long buf) {
  SYS_SYNC_TO_ASYNC_FD(fstatfs64, fd, size, buf);
}

SYS_CAPI_DEF(
  fallocate, 324, long fd, long mode, long off_low, long off_high, long len_low, long len_high) {
  SYS_SYNC_TO_ASYNC_FD(fallocate, fd, mode, off_low, off_high, len_low, len_high);
}

long utime(long path_ref, long times) {
  std::string path((char*)path_ref);
  if (emscripten::is_pthreadfs_file(path)) {
    g_sync_to_async_helper.invoke(
      [path_ref, times](emscripten::sync_to_async::Callback resume) {
        g_resumeFct = [resume]() { (*resume)(); };
        utime_async(path_ref, times, &resumeWrapper_l);
      });
    return resume_result_long;
  }
  return utime_sync(path_ref, times);
}

// Define global variables to be populated by resume;
std::function<void()> g_resumeFct;
emscripten::sync_to_async g_sync_to_async_helper __attribute__((init_priority(102)));

// Other helper code

void emscripten_init_pthreadfs() {
  EM_ASM(console.log('Calling emscripten_init_pthreadfs() is no longer necessary'););
  return;
}

void pthreadfs_load_package(const char* package_path) {
  g_sync_to_async_helper.invoke([package_path](emscripten::sync_to_async::Callback resume) {
    g_resumeFct = [resume]() { (*resume)(); };
    // clang-format off
    EM_ASM({
      (async() => {
          console.log(`Loading package ${UTF8ToString($1)}`);
          importScripts(UTF8ToString($1));
          await PThreadFS.loadAvailablePackages();
          wasmTable.get($0)(); 
      })(); 
    }, &resumeWrapper_v, package_path);
    // clang-format on
  });
}
