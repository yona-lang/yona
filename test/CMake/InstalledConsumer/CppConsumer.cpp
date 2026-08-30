#include "yona/Support/SourceManager.h"

#include <string>

int main() {
  yona::SourceManager Sources;
  const yona::SourceId Source =
      Sources.addSource("<installed-consumer>", "42\n");
  return Sources.text(Source) == "42\n" ? 0 : 1;
}
