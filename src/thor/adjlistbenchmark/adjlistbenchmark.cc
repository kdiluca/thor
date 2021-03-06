#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <boost/program_options.hpp>

#include <valhalla/midgard/util.h>
#include <valhalla/midgard/logging.h>
#include "config.h"

#include "thor/edgelabel.h"
#include "thor/adjacencylist.h"

using namespace valhalla::midgard;
using namespace valhalla::thor;

namespace bpo = boost::program_options;

unsigned int GetRandom(const unsigned int maxcost) {
  return (unsigned int)(rand01() * maxcost);
}

/**
 * Benchmark of adjacency list. Constructs a large number of random numbers,
 * adds EdgeLabels to the AdjacencyList with those as the sortcost. Then
 * removes them from the list.
 */
int Benchmark(const unsigned int n, const float maxcost,
              const float bucketsize) {
  std::vector<unsigned int> costs(n);
  for (unsigned int i = 0; i < n; i++) {
    costs[i] = (unsigned int)GetRandom(maxcost);
  }

  std::clock_t start1 = std::clock();

  std::priority_queue<EdgeLabel> pqueue;

  for (unsigned int i = 0; i < n; i++) {
    EdgeLabel el;
    el.SetSortCost(costs[i]);
    pqueue.push(el);
  }

  unsigned int count = 0;
  while (!pqueue.empty()) {
    EdgeLabel e = pqueue.top();
    pqueue.pop();
    count++;
  }

  unsigned int msecs1 = (std::clock() - start1) / (double)(CLOCKS_PER_SEC / 1000);
  LOG_INFO("Priority Queue: Added and removed " + std::to_string(count) + " edgelabels in " +
    std::to_string(msecs1) + " ms");

  std::clock_t start = std::clock();

  AdjacencyList adjlist(0, maxcost / 2, bucketsize);

  // Construct EdgeLabels and add to adjacency list
  for (unsigned int i = 0; i < n; i++) {
    EdgeLabel* edgelabel = new EdgeLabel();
    edgelabel->SetSortCost(costs[i]);
    adjlist.Add(edgelabel);
  }

//  unsigned int count = 0;
  EdgeLabel* edge;
  while ((edge = adjlist.Remove()) != nullptr) {
    delete edge;
    count++;
  }
  unsigned int msecs = (std::clock() - start) / (double)(CLOCKS_PER_SEC / 1000);
  LOG_INFO("Added and removed " + std::to_string(count) + " edgelabels in " +
    std::to_string(msecs1) + " ms");

  return 0;
}

int main(int argc, char *argv[])
{

  bpo::options_description options(
  "valhalla " VERSION "\n"
  "\n"
  " Usage: valhalla [options]\n"
  "\n"
  "valhalla is simply a program that fronts a stubbed out library in the "
  "autotools framework. The purpose of it is to have a standard configuration "
  "for any new project that one might want to do using autotools and c++11. "
  "It includes some pretty standard dependencies for convenience but those "
  "may or may not be actual requirements for valhalla to build. "
  "\n"
  "\n");


  std::string echo;

  options.add_options()
    ("help,h", "Print this help message.")
    ("version,v", "Print the version of this software.")
    ;

  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(argc,argv)
      .options(options)
      .positional(pos_options)
      .run(),
      vm);
    bpo::notify(vm);

  } catch (std::exception &e) {
    std::cerr << "Unable to parse command line options because: " << e.what() << "\n" << "This is a bug, please report it at " PACKAGE_BUGREPORT << "\n";
    return EXIT_FAILURE;
  }

  if (vm.count("help")) {
    std::cout << options << "\n";
    return EXIT_SUCCESS;
  }

  if (vm.count("version")) {
    std::cout << "AdjacencyListBenchmark " << VERSION << "\n";
    return EXIT_SUCCESS;
  }

  // Benchmark with count, maxcost, and bucketsize
  Benchmark(500000, 50000, 5);
  LOG_INFO("Done Benchmark!");

  return EXIT_SUCCESS;
}
