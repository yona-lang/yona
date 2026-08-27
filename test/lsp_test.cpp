#include "ModuleSource.h"
#include "lsp/Analysis.h"
#include "lsp/Json.h"
#include "lsp/JsonRpc.h"
#include "lsp/Server.h"
#include "lsp/Utf16.h"
#include "repo_paths.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace yona::lsp;

TEST_CASE("UTF-16 mapper: ASCII") {
    auto p = offset_to_position("ab\ncd", 4);
    CHECK(p.line == 1);
    CHECK(p.character == 1);
    CHECK(position_to_offset("ab\ncd", Position{1, 1}) == 4);
}

TEST_CASE("UTF-16 mapper: non-BMP emoji") {
    // U+1F600 is F0 9F 98 80 — one codepoint, two UTF-16 units
    std::string s = "a\xF0\x9F\x98\x80" "b";
    auto p = offset_to_position(s, 5); // after emoji
    CHECK(p.line == 0);
    CHECK(p.character == 3);
    CHECK(position_to_offset(s, Position{0, 3}) == 5);
}

TEST_CASE("UTF-16 mapper: CRLF") {
    auto p = offset_to_position("a\r\nb", 3);
    CHECK(p.line == 1);
    CHECK(p.character == 0);
}

TEST_CASE("JSON parse and dump objects") {
    auto j = Json::parse(R"({"id":1,"method":"initialize","params":{"x":true}})");
    CHECK(j.is_object());
    CHECK(j.get("id").as_int() == 1);
    CHECK(j.get("method").as_string() == "initialize");
    CHECK(j.get("params").get("x").as_bool());
    auto dumped = j.dump();
    CHECK(dumped.find("\"method\":\"initialize\"") != std::string::npos);
}

TEST_CASE("JSON-RPC Content-Length framing") {
    Json body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    body["result"] = nullptr;
    auto framed = JsonRpc::encode(body);
    CHECK(framed.find("Content-Length:") == 0);
    std::istringstream in(framed);
    auto read = JsonRpc::read_body(in);
    REQUIRE(read);
    auto msg = JsonRpc::parse_message(*read);
    REQUIRE(msg);
    CHECK(msg->has_id);
}

TEST_CASE("Analysis diagnostics for undefined variable") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "unknown_name");
    auto diags = a.diagnostics();
    bool found = false;
    for (const auto& d : diags) {
        if (d.code == "E0103" || d.message.find("undefined") != std::string::npos)
            found = true;
    }
    CHECK(found);
}

TEST_CASE("Analysis hover on let binding") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "let answer = 42 in answer");
    auto hover = a.hover(Position{0, 5});
    REQUIRE(hover);
    CHECK(hover->contents.find("answer") != std::string::npos);
}

TEST_CASE("Analysis hover does not match the exclusive end of a name") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "let answer = 42 in answer");
    // "answer" is LSP [4, 10); character 10 is the space after the binding
    auto hover = a.hover(Position{0, 10});
    CHECK_FALSE(hover);
}

TEST_CASE("is_module_source skips # and ## then requires a module token") {
    CHECK(yona::is_module_source("module Foo"));
    CHECK(yona::is_module_source("## docs\nmodule Std\\List"));
    CHECK(yona::is_module_source("# comment\n  module X"));
    CHECK_FALSE(yona::is_module_source("let x = 1 in x"));
    CHECK_FALSE(yona::is_module_source("modulex = 1"));
    CHECK_FALSE(yona::is_module_source("module_x = 1"));
}

TEST_CASE("Analysis definition and references") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "let answer = 42 in answer");
    auto defs = a.definition(Position{0, 20});
    CHECK(!defs.empty());
    auto refs = a.references(Position{0, 5}, true);
    CHECK(refs.size() >= 1);
}

TEST_CASE("Analysis completion includes keywords") {
    Analysis a;
    a.analyze("file:///tmp/t.yona", "let x = 1 in x");
    auto items = a.completions(Position{0, 0});
    bool has_let = false;
    for (const auto& it : items) {
        if (it.get("label").as_string() == "let")
            has_let = true;
    }
    CHECK(has_let);
}

TEST_CASE("Analysis document symbols") {
    Analysis a;
    a.analyze("file:///tmp/t.yona", "let answer = 42 in answer");
    auto syms = a.document_symbols();
    bool found = false;
    for (const auto& s : syms) {
        if (s.name == "answer")
            found = true;
    }
    CHECK(found);
}

TEST_CASE("LSP trait symbols retain declaration metadata") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/traits.yona", R"(
module Test\Traits

trait Eq a
    eq : a -> a -> Bool
end

instance Eq Int
    eq left right = left == right
end
)");

    auto symbols = a.document_symbols();
    const SymbolInfo* trait = nullptr;
    const SymbolInfo* method = nullptr;
    const SymbolInfo* instance = nullptr;
    for (const auto& symbol : symbols) {
        if (symbol.name == "Eq" && symbol.kind == "interface")
            trait = &symbol;
        if (symbol.name == "eq" && symbol.kind == "method")
            method = &symbol;
        if (symbol.name == "Eq Int" && symbol.kind == "instance")
            instance = &symbol;
    }
    REQUIRE(trait);
    CHECK(trait->type.find("trait Eq a") != std::string::npos);
    REQUIRE(method);
    CHECK(method->container == "Eq");
    CHECK(method->type == "a -> a -> Bool");
    REQUIRE(instance);
    CHECK(instance->type == "instance Eq Int");

    auto trait_hover = a.hover(Position{3, 7});
    REQUIRE(trait_hover);
    CHECK(trait_hover->contents.find("trait Eq a") != std::string::npos);
    auto method_hover = a.hover(Position{4, 5});
    REQUIRE(method_hover);
    CHECK(method_hover->contents.find("a -> a -> Bool") != std::string::npos);
}

TEST_CASE("LSP trait operations use trait-specific kinds and identities") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/traits.yona", R"(
module Test\Traits

trait Eq a
    eq : a -> a -> Bool
end

instance Eq Int
    eq left right = left == right
end
)");

    auto items = a.completions(Position{0, 0});
    int eq_kind = 0;
    int method_kind = 0;
    for (const auto& item : items) {
        if (item.get("label").as_string() == "Eq")
            eq_kind = item.get("kind").as_int();
        if (item.get("label").as_string() == "eq")
            method_kind = item.get("kind").as_int();
    }
    CHECK(eq_kind == 8);     // LSP Interface
    CHECK(method_kind == 2); // LSP Method

    auto signature = a.signature_help(Position{4, 5});
    REQUIRE(signature);
    CHECK(signature->get("signatures").as_array().at(0).get("label").as_string() ==
          "eq : a -> a -> Bool");

    auto tokens = a.semantic_tokens();
    bool saw_method_function_token = false;
    std::size_t line = 0;
    std::size_t column = 0;
    for (std::size_t i = 0; i < tokens.size(); i += 5) {
        line += tokens[i];
        column = tokens[i] == 0 ? column + tokens[i + 1] : tokens[i + 1];
        if (line == 4 && column == 4 && tokens[i + 3] == 0)
            saw_method_function_token = true;
    }
    CHECK(saw_method_function_token);

    Json edits;
    auto renamed = a.rename(Position{4, 5}, "same", edits);
    REQUIRE(renamed);
    CHECK(*renamed == "eq");
    CHECK(edits.as_array().at(0).get("edits").as_array().size() == 1);
}

TEST_CASE("LSP trait diagnostics offer an explicit instance explanation") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/missing.yona", R"(
module Test\Missing

trait Measure a
    measure : a -> Int
end

run _ = measure "x"
)");

    auto diagnostics = a.diagnostics();
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "E0106");
    auto actions = a.code_actions(Range{{0, 0}, {100, 1000}});
    REQUIRE(actions.size() == 1);
    CHECK(actions[0].get("title").as_string() == "Explain trait instance E0106");
    CHECK(actions[0].get("command").get("command").as_string() == "yona.explain");
    CHECK(actions[0].get("command").get("arguments").as_array().at(0).as_string() == "E0106");
}

TEST_CASE("LSP imported interface contract preserves UTF-16 navigation") {
    Analysis a;
    a.set_module_paths({yona::test::lib_dir().string()});
    const std::string source =
        "import identity from Prelude in let marker = \"\xF0\x9F\x98\x80\" in identity 2";
    a.analyze("file:///tmp/imported-trait.yona", source);

    CHECK(a.diagnostics().empty());
    bool has_compare_completion = false;
    for (const auto& item : a.completions(Position{0, 0}))
        has_compare_completion = has_compare_completion || item.get("label").as_string() == "identity";
    CHECK(has_compare_completion);
    CHECK(a.hover(offset_to_position(source, source.find("identity"))).has_value());
    const auto use = offset_to_position(source, source.rfind("identity"));
    CHECK(use.line == 0);
    CHECK(use.character > 32); // The preceding emoji occupies two UTF-16 code units.
    CHECK(a.hover(use).has_value());
    auto definition = a.definition(use);
    CHECK(definition.size() == 1);
    if (!definition.empty())
        CHECK(definition[0].uri.find("Prelude") != std::string::npos);
    auto signature = a.signature_help(use);
    REQUIRE(signature);
    CHECK(signature->get("signatures").as_array().at(0).get("label").as_string().find("identity") !=
          std::string::npos);
}

TEST_CASE("Server initialize and hover") {
    Server srv;
    RpcMessage init;
    init.has_id = true;
    init.id = 1;
    init.method = "initialize";
    init.params = Json::Object{};
    auto cap = srv.handle(init);
    CHECK(cap.get("capabilities").get("hoverProvider").as_bool());

    Json td;
    td["uri"] = "file:///tmp/t.yona";
    td["text"] = "let answer = 42 in answer";
    Json params;
    params["textDocument"] = td;
    RpcMessage open;
    open.method = "textDocument/didOpen";
    open.params = params;
    srv.handle(open);

    Json pos;
    pos["line"] = 0;
    pos["character"] = 5;
    Json hp;
    hp["textDocument"] = Json::Object{{"uri", Json("file:///tmp/t.yona")}};
    hp["position"] = pos;
    RpcMessage hover;
    hover.has_id = true;
    hover.id = 2;
    hover.method = "textDocument/hover";
    hover.params = hp;
    auto hv = srv.handle(hover);
    CHECK(hv.get("contents").get("value").as_string().find("answer") != std::string::npos);
}

TEST_CASE("Server publishDiagnostics notification") {
    Server srv;
    Json td;
    td["uri"] = "file:///tmp/bad.yona";
    td["text"] = "unknown_name";
    Json params;
    params["textDocument"] = td;
    RpcMessage open;
    open.method = "textDocument/didOpen";
    open.params = params;
    srv.handle(open);
    auto note = srv.diagnostics_notification("file:///tmp/bad.yona");
    CHECK(note.get("method").as_string() == "textDocument/publishDiagnostics");
    CHECK(!note.get("params").get("diagnostics").as_array().empty());
}

TEST_CASE("JSON decodes BMP unicode escape") {
    auto j = Json::parse(R"("\u0041")");
    CHECK(j.as_string() == "A");
}

TEST_CASE("JSON decodes UTF-16 surrogate pair") {
    // U+1F600 GRINNING FACE as a JSON surrogate pair
    auto j = Json::parse(R"("\uD83D\uDE00")");
    CHECK(j.as_string() == "\xF0\x9F\x98\x80");
}

TEST_CASE("JSON rejects nesting deeper than 64") {
    std::string ok;
    ok.assign(64, '[');
    ok.append(64, ']');
    std::string ok_err;
    auto ok_json = Json::parse(ok, &ok_err);
    CHECK(ok_json.is_array());
    CHECK(ok_err.empty());

    std::string too;
    too.assign(65, '[');
    too.append(65, ']');
    std::string err;
    auto deep = Json::parse(too, &err);
    CHECK(deep.is_null());
    CHECK(err.find("deep") != std::string::npos);
}

TEST_CASE("UTF-16 mapper: combining character is its own unit") {
    // U+0065 LATIN SMALL LETTER E + U+0301 COMBINING ACUTE ACCENT + 'x'
    std::string s = "e\xCC\x81x";
    CHECK(offset_to_position(s, 3).character == 2);
    CHECK(position_to_offset(s, Position{0, 2}) == 3);
}

static bool has_parse_error(const std::vector<LspDiagnostic>& diags) {
    for (const auto& d : diags) {
        if (d.code == "E0301")
            return true;
    }
    return false;
}

TEST_CASE("Analysis treats leading ## docs as a module") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/List.yona", "## docs\nmodule Std\\List\nexport x\nx = 1");
    CHECK_FALSE(has_parse_error(a.diagnostics()));
    bool found_x = false;
    for (const auto& s : a.document_symbols()) {
        if (s.name == "x")
            found_x = true;
    }
    CHECK(found_x);
}

TEST_CASE("Analysis rename updates a function parameter") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src =
        "## docs\n"
        "module Test\\Map\n"
        "export map\n"
        "map fn seq = case seq of\n"
        "  [] -> []\n"
        "  [h|t] -> fn h\n"
        "end\n";
    a.analyze("file:///tmp/Map.yona", src);
    CHECK_FALSE(has_parse_error(a.diagnostics()));
    // Line 3: "map fn seq = case seq of" — parameter `fn` starts at column 4
    Json edits;
    auto old = a.rename(Position{3, 4}, "f", edits);
    REQUIRE(old);
    CHECK(*old == "fn");
    REQUIRE(edits.is_array());
    REQUIRE(!edits.as_array().empty());
    Json file0 = edits.as_array()[0];
    Json file_edits = file0.get("edits");
    REQUIRE(file_edits.as_array().size() >= 2);
    bool saw_param = false;
    bool saw_use = false;
    for (const auto& e : file_edits.as_array()) {
        Json start = e.get("range").get("start");
        CHECK(e.get("newText").as_string() == "f");
        if (start.get("line").as_int() == 3 && start.get("character").as_int() == 4)
            saw_param = true;
        if (start.get("line").as_int() == 5)
            saw_use = true;
    }
    CHECK(saw_param);
    CHECK(saw_use);
}

TEST_CASE("Analysis signature help looks behind a space") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "identity 1");
    auto help = a.signature_help(Position{0, 9});
    REQUIRE(help);
    CHECK(help->get("signatures").as_array().at(0).get("label").as_string().find("identity") !=
          std::string::npos);
}

TEST_CASE("default_module_paths includes workspace roots") {
    auto paths = default_module_paths("/tmp/doc.yona", {"/workspace/root"});
    bool found = false;
    for (const auto& p : paths) {
        if (p.find("workspace") != std::string::npos && p.find("root") != std::string::npos)
            found = true;
    }
    CHECK(found);
}

TEST_CASE("Server initialize uses workspace folders and version") {
    Server srv;
    RpcMessage init;
    init.has_id = true;
    init.id = 1;
    init.method = "initialize";
    Json folder;
    folder["uri"] = "file:///workspace/root";
    folder["name"] = "root";
    init.params = Json::Object{
        {"rootUri", Json("file:///workspace/root")},
        {"workspaceFolders", Json::Array{folder}},
    };
    auto cap = srv.handle(init);
    CHECK(cap.get("capabilities").get("hoverProvider").as_bool());
    CHECK(!cap.get("serverInfo").get("version").as_string().empty());

    Json td;
    td["uri"] = "file:///workspace/root/t.yona";
    td["text"] = "1";
    RpcMessage open;
    open.method = "textDocument/didOpen";
    open.params = Json::Object{{"textDocument", td}};
    srv.handle(open);
    CHECK(srv.diagnostics_notification("file:///workspace/root/t.yona")
              .get("method")
              .as_string() == "textDocument/publishDiagnostics");
}

TEST_CASE("Server reanalyzes open buffers on watched file change") {
    Server srv;
    Json td;
    td["uri"] = "file:///tmp/t.yona";
    td["text"] = "unknown_name";
    RpcMessage open;
    open.method = "textDocument/didOpen";
    open.params = Json::Object{{"textDocument", td}};
    srv.handle(open);

    RpcMessage watch;
    watch.method = "workspace/didChangeWatchedFiles";
    Json ev;
    ev["uri"] = "file:///tmp/other.yona";
    ev["type"] = 2;
    watch.params = Json::Object{{"changes", Json::Array{ev}}};
    auto result = srv.handle(watch);
    CHECK_FALSE(result.is_null());
    auto note = srv.diagnostics_notification("file:///tmp/t.yona");
    CHECK(!note.get("params").get("diagnostics").as_array().empty());
}

TEST_CASE("Analysis semantic tokens are a multiple of five") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "let answer = 42 in answer");
    auto toks = a.semantic_tokens();
    CHECK(!toks.empty());
    CHECK(toks.size() % 5 == 0);
}

TEST_CASE("Analysis inlay hints overlap a query that misses name endpoints") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "let f = identity in f");
    auto all = a.inlay_hints(Range{{0, 0}, {0, 40}});
    auto inner = a.inlay_hints(Range{{0, 4}, {0, 5}});
    auto after = a.inlay_hints(Range{{1, 0}, {1, 1}});
    CHECK(inner.size() == all.size());
    CHECK(after.empty());
    if (!all.empty())
        CHECK(all[0].get("label").as_string().find(" : ") == 0);
}

TEST_CASE("Analysis explain code action uses the diagnostic range") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "unknown_name");
    auto acts = a.code_actions(Range{{0, 0}, {0, 1}});
    bool found = false;
    for (const auto& act : acts) {
        if (act.get("command").get("command").as_string() == "yona.explain")
            found = true;
    }
    CHECK(found);
}

static std::filesystem::path write_mod(const std::filesystem::path& dir, const std::string& rel,
                                       const std::string& body) {
    auto p = dir / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream o(p);
    o << body;
    return p;
}

TEST_CASE("Analysis definition follows import to the source module") {
    auto dir = std::filesystem::temp_directory_path() / "yona_yls_import_def";
    write_mod(dir, "Orig/Mod.yona", "module Orig\\Mod\nexport answer\nanswer = 42\n");
    Analysis a;
    a.set_module_paths({dir.string()});
    const std::string src = "import answer from Orig\\Mod in answer";
    a.analyze("file:///tmp/use.yona", src);
    auto pos = offset_to_position(src, src.rfind("answer"));
    auto defs = a.definition(pos);
    REQUIRE(!defs.empty());
    CHECK(defs[0].uri.find("Orig") != std::string::npos);
    CHECK(defs[0].uri.find("Mod.yona") != std::string::npos);
}

TEST_CASE("Analysis definition follows FQN module call") {
    auto dir = std::filesystem::temp_directory_path() / "yona_yls_fqn_def";
    write_mod(dir, "Orig.yona", "module Orig\nexport answer\nanswer = 42\n");
    Analysis a;
    a.set_module_paths({dir.string()});
    const std::string src = "Orig.answer";
    a.analyze("file:///tmp/use.yona", src);
    auto pos = offset_to_position(src, src.rfind("answer"));
    auto defs = a.definition(pos);
    REQUIRE(!defs.empty());
    CHECK(defs[0].uri.find("Orig.yona") != std::string::npos);
}

TEST_CASE("Analysis definition follows FQN call in the defining module") {
    auto dir = std::filesystem::temp_directory_path() / "yona_yls_self_fqn";
    auto p = write_mod(dir, "Orig.yona",
                       "module Orig\nexport answer\nanswer = 42\nuse = Orig.answer\n");
    std::ifstream in(p);
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Analysis a;
    a.set_module_paths({dir.string()});
    a.analyze(file_uri(p.generic_string()), src);
    auto pos = offset_to_position(src, src.rfind("answer"));
    auto defs = a.definition(pos);
    REQUIRE(!defs.empty());
    CHECK(defs[0].uri == file_uri(p.generic_string()));
    CHECK(defs[0].range.start.line == 2);
}

TEST_CASE("Analysis definition follows aliased module call") {
    auto dir = std::filesystem::temp_directory_path() / "yona_yls_alias_fqn";
    write_mod(dir, "Orig/Mod.yona", "module Orig\\Mod\nexport answer\nanswer = 42\n");
    Analysis a;
    a.set_module_paths({dir.string()});
    const std::string src = "import Orig\\Mod as O in O.answer";
    a.analyze("file:///tmp/use.yona", src);
    auto pos = offset_to_position(src, src.rfind("answer"));
    auto defs = a.definition(pos);
    REQUIRE(!defs.empty());
    CHECK(defs[0].uri.find("Orig") != std::string::npos);
    CHECK(defs[0].uri.find("Mod.yona") != std::string::npos);
}

TEST_CASE("Analysis local binding shadows imported name") {
    auto dir = std::filesystem::temp_directory_path() / "yona_yls_import_shadow";
    write_mod(dir, "Orig/Mod.yona", "module Orig\\Mod\nexport answer\nanswer = 42\n");
    Analysis a;
    a.set_module_paths({dir.string()});
    const std::string src = "import answer from Orig\\Mod in let answer = 1 in answer";
    a.analyze("file:///tmp/use.yona", src);
    auto pos = offset_to_position(src, src.rfind("answer"));
    auto defs = a.definition(pos);
    REQUIRE(!defs.empty());
    CHECK(defs[0].uri == "file:///tmp/use.yona");
    CHECK(defs[0].uri.find("Mod.yona") == std::string::npos);
}

TEST_CASE("Analysis document highlight marks local def and use") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src = "let answer = 42 in answer";
    a.analyze("file:///tmp/t.yona", src);
    auto hs = a.document_highlight(offset_to_position(src, src.rfind("answer")));
    CHECK(hs.size() >= 2);
    bool saw_write = false, saw_read = false;
    for (const auto& h : hs) {
        if (h.kind == 3)
            saw_write = true;
        if (h.kind == 2)
            saw_read = true;
    }
    CHECK(saw_write);
    CHECK(saw_read);
}

TEST_CASE("Analysis rename of an imported name does not edit the source module") {
    auto dir = std::filesystem::temp_directory_path() / "yona_yls_import_rename";
    write_mod(dir, "Orig/Mod.yona", "module Orig\\Mod\nexport answer\nanswer = 42\n");
    Analysis a;
    a.set_module_paths({dir.string()});
    const std::string src = "import answer from Orig\\Mod in answer";
    a.analyze("file:///tmp/use.yona", src);
    Json edits;
    auto old = a.rename(offset_to_position(src, src.rfind("answer")), "ans", edits);
    REQUIRE(old);
    CHECK(*old == "answer");
    REQUIRE(edits.is_array());
    for (const auto& file : edits.as_array()) {
        auto uri = file.get("textDocument").get("uri").as_string();
        CHECK(uri.find("Orig") == std::string::npos);
        CHECK(uri.find("Mod.yona") == std::string::npos);
    }
}

TEST_CASE("Server documentHighlight capability") {
    Server srv;
    RpcMessage init;
    init.has_id = true;
    init.id = 1;
    init.method = "initialize";
    init.params = Json::Object{};
    auto cap = srv.handle(init);
    CHECK(cap.get("capabilities").get("documentHighlightProvider").as_bool());
}

static bool has_completion_label(const std::vector<Json>& items, const std::string& label) {
    for (const auto& it : items) {
        if (it.get("label").as_string() == label)
            return true;
    }
    return false;
}

static bool all_parse_diagnostics(const std::vector<LspDiagnostic>& diags) {
    if (diags.empty())
        return false;
    for (const auto& d : diags) {
        if (d.code != "E0301")
            return false;
    }
    return true;
}

TEST_CASE("Analysis recovers hover from incomplete let body") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "let answer = 42 in");
    CHECK(all_parse_diagnostics(a.diagnostics()));
    auto hover = a.hover(Position{0, 5});
    REQUIRE(hover);
    CHECK(hover->contents.find("answer") != std::string::npos);
}

TEST_CASE("Analysis recovers hover after trailing binary op") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src = "let answer = 42 in answer +";
    a.analyze("file:///tmp/t.yona", src);
    CHECK(all_parse_diagnostics(a.diagnostics()));
    auto hover = a.hover(offset_to_position(src, src.find("answer")));
    REQUIRE(hover);
    CHECK(hover->contents.find("answer") != std::string::npos);
}

TEST_CASE("Analysis recovers hover from incomplete if") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src = "let answer = 42 in if answer";
    a.analyze("file:///tmp/t.yona", src);
    CHECK(all_parse_diagnostics(a.diagnostics()));
    auto hover = a.hover(offset_to_position(src, src.rfind("answer")));
    REQUIRE(hover);
    CHECK(hover->contents.find("answer") != std::string::npos);
}

TEST_CASE("Analysis recovers hover from incomplete do") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src = "do\n  let x = 1 in x";
    a.analyze("file:///tmp/t.yona", src);
    CHECK(all_parse_diagnostics(a.diagnostics()));
    auto hover = a.hover(Position{1, 6});
    REQUIRE(hover);
    CHECK(hover->contents.find("x") != std::string::npos);
}

TEST_CASE("Analysis recovers hover from incomplete case") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src = "let answer = 42 in case answer of";
    a.analyze("file:///tmp/t.yona", src);
    CHECK(all_parse_diagnostics(a.diagnostics()));
    auto hover = a.hover(offset_to_position(src, src.rfind("answer")));
    REQUIRE(hover);
    CHECK(hover->contents.find("answer") != std::string::npos);
}

TEST_CASE("Analysis recovery stays empty when nothing parses") {
    Analysis a;
    a.analyze("file:///tmp/t.yona", ")");
    CHECK(has_parse_error(a.diagnostics()));
    CHECK_FALSE(a.hover(Position{0, 0}));
    CHECK(a.document_symbols().empty());
    CHECK(a.definition(Position{0, 0}).empty());
    CHECK(has_completion_label(a.completions(Position{0, 0}), "let"));
}

TEST_CASE("Analysis successful parse is unchanged by recovery") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/t.yona", "let answer = 42 in answer");
    CHECK_FALSE(has_parse_error(a.diagnostics()));
    auto hover = a.hover(Position{0, 5});
    REQUIRE(hover);
    CHECK(hover->contents.find("answer") != std::string::npos);
    CHECK(has_completion_label(a.completions(Position{0, 0}), "answer"));
}

TEST_CASE("Analysis recovered prefix still defines highlights and completes") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src = "let answer = 42 in";
    a.analyze("file:///tmp/t.yona", src);
    CHECK(all_parse_diagnostics(a.diagnostics()));
    auto defs = a.definition(Position{0, 5});
    REQUIRE(!defs.empty());
    auto hs = a.document_highlight(Position{0, 5});
    CHECK(!hs.empty());
    CHECK(has_completion_label(a.completions(Position{0, 18}), "answer"));
    CHECK(has_completion_label(a.completions(Position{0, 18}), "let"));
}

TEST_CASE("Analysis recovers module incomplete function binding") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    const std::string src = "module Foo\nexport answer\nanswer = 1 +";
    a.analyze("file:///tmp/Mod.yona", src);
    CHECK(all_parse_diagnostics(a.diagnostics()));
    auto hover = a.hover(offset_to_position(src, src.rfind("answer")));
    REQUIRE(hover);
    CHECK(hover->contents.find("answer") != std::string::npos);
}

TEST_CASE("Analysis publishes nested unreachable-pattern warnings") {
    Analysis a;
    a.set_module_paths(default_module_paths(""));
    a.analyze("file:///tmp/overlap.yona", "case Some 1 of Some _ -> 1; Some 1 -> 2; None -> 0 end");
    auto diags = a.diagnostics();
    const auto warning = std::find_if(diags.begin(), diags.end(), [](const LspDiagnostic& d) {
        return d.severity == 2 && d.message.find("unreachable pattern") != std::string::npos;
    });
    REQUIRE(warning != diags.end());
    CHECK(warning->range.start.character > 25);
}
