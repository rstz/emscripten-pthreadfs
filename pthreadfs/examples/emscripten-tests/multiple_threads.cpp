// basic file operations
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <emscripten.h>



void threadMain(int msg) {
  size_t thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
  std::ofstream myfile;
  myfile.open ("persistent/multi_threading_example", std::ios_base::app);
  myfile << "Writing from thread " << msg << " Id: " << thread_id << "   ";
  myfile.close();
  EM_ASM({console.log(`Wrote on thread ${$0}`);}, thread_id);
  return;
}

int main () {
  EM_ASM({console.log("Hello from main");});
  std::remove("persistent/multi_threading_example"); 

  constexpr int number_of_threads = 10;

  std::cout << "Proof that stdout works fine.\n";

  std::vector<std::thread> threads;

  for (int i = 0; i< number_of_threads; i++) {
    std::thread thread(threadMain, i);
    threads.push_back(std::move(thread));
  }

  std::ofstream myfile;
  myfile.open ("persistent/multi_threading_example");
  myfile << "Writing the main thread.\n";
  myfile.close();
  
  for (int i = 0; i< number_of_threads; i++) {
    threads[i].join();
  }

  EM_ASM({
    console.log('Remember to check that the contents of file multi_threading_example are correct.');
  });
  std::cout << "Success\n";

  return 0;
}