// basic file operations
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/statfs.h>
#include <string>
#include <emscripten.h>
#include "pthreadfs.h"

int main () {
  emscripten_init_pthreadfs();
  std::cout << "Proof that stdout works fine.\n";
  std::ofstream myfile;
  myfile.open ("pthreadfs/example");
  myfile << "Writing a few characters.\n";
  myfile.close();
  struct statfs sb;
  if((statfs("pthreadfs/example",&sb))==0){
    std::cout << "total file nodes in fs are " << sb.f_files << "\n";
  }
  return 0;
}