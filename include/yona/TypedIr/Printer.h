#ifndef YONA_TYPEDIR_PRINTER_H
#define YONA_TYPEDIR_PRINTER_H
#include "yona/TypedIr/TypedIr.h"

#include <string>
namespace yona::typed_ir {
[[nodiscard]] std::string printModule(const Module &);
}
#endif
