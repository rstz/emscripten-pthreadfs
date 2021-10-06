/*
 * Copyright 2013 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void cleanup() {
  rmdir("persistent/mainthreadfolder/subfolder");
  rmdir("persistent/mainthreadfolder");
}

void test_file_contents(const char* path) {
  printf("Test contents for file %s\n", path);

  int fd = open(path, O_RDONLY, 0777);
  assert(fd >= 0);

  char readbuf[1000];
  int err = read(fd, readbuf, sizeof(char) * (strlen(path) + 20));
  assert(err > 0);
  printf("Content: %s\n", readbuf);
  close(fd);

  unlink(path);
}

int main() {
  const char* paths[] = {"persistent/file.txt", "persistent/mainthreadfolder/subfolder/ok now"};

  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    test_file_contents(paths[i]);
  }
  cleanup();

  puts("success");
  return EXIT_SUCCESS;
}