#include "yona/TypedCore/Abi.h"

#include <stddef.h>

int main(void) {
  YonaTypedCoreModule *Module =
      YonaTypedCoreAnalyze(NULL, NULL, NULL, (size_t)0);
  YonaTypedCoreDisposeModule(Module);
  return Module == NULL ? 0 : 1;
}
