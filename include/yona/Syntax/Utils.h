//
// Created by akovari on 15.12.24.
//

#ifndef YONA_SYNTAX_UTILS_H
#define YONA_SYNTAX_UTILS_H

#include "yona/Support/Common.h"
#include "yona/Syntax/Ast.h"

#define PACKAGE_DELIMITER "\\"
#define NAME_DELIMITER "::"

namespace yona {
using std::initializer_list;
using std::make_shared;
using std::map;
using std::pair;
using std::shared_ptr;
using std::string;
using std::vector;

const vector<pair<string, string>> YONA_CTRL_CHARS_UNESCAPE = {
    {"\\b", "\b"}, {"\\n", "\n"}, {"\\t", "\t"},
    {"\\f", "\f"}, {"\\r", "\r"}, {"\\0", "\0"}};

/// Polymorphic string transformer. Inputs are borrowed for a call and results
/// are owned. Implementations define their own failure and thread-safety rules.
class CharSequenceTranslator {
public:
  CharSequenceTranslator() = default;
  virtual ~CharSequenceTranslator() = default;
  virtual string translate(const string &input) = 0;
};

/// Immutable-after-construction lookup transformer, safe for concurrent calls.
class LookupTranslator final : public CharSequenceTranslator {
private:
  map<string, string> lookupMap;

public:
  explicit LookupTranslator(const vector<pair<string, string>> &lookup)
      : lookupMap(lookup.begin(), lookup.end()) {};

  string translate(const string &input) override;
};

/// Retains shared ownership of its ordered child translators.
///
/// Translation failures propagate as exceptions. Concurrent use is valid only
/// when every retained translator supports concurrent calls.
class AggregateTranslator final : public CharSequenceTranslator {
private:
  vector<shared_ptr<CharSequenceTranslator>> translators;

public:
  AggregateTranslator(
      const initializer_list<shared_ptr<CharSequenceTranslator>> translators)
      : translators(translators) {}

  string translate(const string &input) override;
};

inline auto UNESCAPE_YONA = AggregateTranslator{
    // new OctalUnescaper(), // .between('\1', '\377')
    // new UnicodeUnescaper(),
    make_shared<LookupTranslator>(YONA_CTRL_CHARS_UNESCAPE),
    // make_shared<LookupTranslator>({ { "\\\"", "\"" },
    //                                 { "\\\\", "\\" },
    //                                 { "\\'", "'" },
    //                                 { "{{", "{" },
    //                                 { "}}", "}" },
    //                                 { "\\a" /*Bell (alert)*/, string(1,
    //                                 (char)7) },
    //                                 { "\\v" /*Vertical tab*/, string(1,
    //                                 (char)9) } }
    //                                 )
};

/// Return an owned string after applying the process-wide Yona escape table.
/// The current built-in translator is immutable and supports concurrent calls.
string unescapeYonaString(const string &rawString);

/// Return an owned generic path ending in `.yona`. Components are borrowed
/// only for the call; allocation and filesystem path exceptions propagate.
string module_location(const vector<string> &module_name);
} // namespace yona

#endif /* YONA_SYNTAX_UTILS_H */
