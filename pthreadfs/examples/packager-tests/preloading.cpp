/*
 * Copyright 2021 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 */


#include <assert.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

int test(std::string file_path, std::string first_line, int size) {
  std::cout << "Start reading first line of file " << file_path << std::endl;
  std::ifstream stream(file_path);
  std::string read_line;
  std::getline(stream, read_line);
  stream.close();

  std::cout << "  " << read_line << std::endl;
  assert(read_line == first_line);

  struct stat s;
  int err = stat(file_path.c_str(), &s);
  assert(!err);
  assert(s.st_size == size);
  
  return 0;
}

int main() {
  std::cout << "Test preloading files.\n";

  test("persistent/smallfile.txt", "These are the contents of a very small file.", 188);
  test("persistent/Subfolder/mediumfile.txt",
    "Begin mediumfile.txt -------------------------------------------", 138670);
  test("persistent/bigfile.txt", "Begin bigfile.txt ----------------------------------------------",
    212992000);

  std::cout << "Success.\n";
  return 0;
}