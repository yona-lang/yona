#include "typed_core/Query.h"

#include <doctest/doctest.h>

TEST_CASE("typed-core query types have no LLVM dependency") {
    yona::typed_core::Hover hover;
    hover.contents = "answer : Int";
    hover.range.start = yona::typed_core::Position{0, 4};
    hover.range.end = yona::typed_core::Position{0, 10};
    CHECK(hover.contents.find("Int") != std::string::npos);
    CHECK(hover.range.end.character > hover.range.start.character);

    yona::typed_core::Diagnostic d;
    d.code = "E0103";
    d.message = "undefined variable";
    CHECK(d.severity == 1);
}
