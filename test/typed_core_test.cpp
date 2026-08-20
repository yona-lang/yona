#include "typed_core/Query.h"

#include <doctest/doctest.h>

TEST_CASE("LSP ranges are end-exclusive") {
    yona::typed_core::Range r{{0, 4}, {0, 10}};
    CHECK(r.contains({0, 4}));
    CHECK(r.contains({0, 9}));
    CHECK_FALSE(r.contains({0, 10}));
    CHECK_FALSE(r.contains({0, 3}));
    CHECK_FALSE(r.contains({1, 0}));
}

TEST_CASE("LSP ranges overlap without sharing endpoints") {
    yona::typed_core::Range name{{0, 4}, {0, 10}};
    yona::typed_core::Range inner{{0, 5}, {0, 6}};
    yona::typed_core::Range after{{0, 10}, {0, 12}};
    CHECK(name.overlaps(inner));
    CHECK(inner.overlaps(name));
    CHECK_FALSE(name.overlaps(after));
}

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
