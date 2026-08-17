// This file provides the main() function for doctest tests
// Used for consistency across all platforms

#define DOCTEST_CONFIG_IMPLEMENT
#include <cfloat>
#include <doctest/doctest.h>

int main(int argc, char **argv) {
  doctest::Context context;

  // Apply command line arguments
  context.applyCommandLine(argc, argv);

  // Run tests
  int res = context.run();

  return res;
}
