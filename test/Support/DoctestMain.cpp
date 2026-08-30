// This file provides the main() function for doctest tests
// Used for consistency across all platforms

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cfloat>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string xml_escape(const char *s) {
  std::string out;
  if (!s)
    return out;
  for (const char *p = s; *p; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&apos;";
      break;
    default:
      if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
        out += '?';
      else
        out += static_cast<char>(c);
    }
  }
  return out;
}

struct RecordedCase {
  std::string suite;
  std::string name;
  std::string status; // started | passed | failed | error | skipped
  std::string message;
  double seconds = 0;
};

class JunitFileListener : public doctest::IReporter {
  std::string path_;
  std::vector<RecordedCase> cases_;
  std::vector<std::string> subcases_;
  std::string pending_message_;
  bool pending_error_ = false;

  static const char *suite_of(const doctest::TestCaseData &in) {
    if (in.m_test_suite && in.m_test_suite[0] != '\0')
      return in.m_test_suite;
    return "(ungrouped)";
  }

  void flush() const {
    if (path_.empty())
      return;
    std::ofstream out(path_.c_str(), std::ios::trunc);
    if (!out)
      return;

    struct SuiteAgg {
      std::string name;
      int tests = 0, failures = 0, errors = 0, skipped = 0;
      std::vector<const RecordedCase *> cases;
    };
    std::vector<SuiteAgg> suites;
    for (const auto &c : cases_) {
      SuiteAgg *dest = nullptr;
      for (auto &s : suites) {
        if (s.name == c.suite) {
          dest = &s;
          break;
        }
      }
      if (!dest) {
        suites.push_back(SuiteAgg{});
        dest = &suites.back();
        dest->name = c.suite;
      }
      dest->cases.push_back(&c);
      dest->tests++;
      if (c.status == "failed")
        dest->failures++;
      else if (c.status == "error" || c.status == "started")
        dest->errors++;
      else if (c.status == "skipped")
        dest->skipped++;
    }

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites>\n";
    for (const auto &s : suites) {
      out << "  <testsuite name=\"" << xml_escape(s.name.c_str())
          << "\" tests=\"" << s.tests << "\" failures=\"" << s.failures
          << "\" errors=\"" << s.errors << "\" skipped=\"" << s.skipped
          << "\">\n";
      for (const auto *c : s.cases) {
        out << "    <testcase classname=\"" << xml_escape(c->suite.c_str())
            << "\" name=\"" << xml_escape(c->name.c_str()) << "\" time=\""
            << c->seconds << "\"";
        if (c->status == "passed") {
          out << "/>\n";
          continue;
        }
        out << ">\n";
        if (c->status == "skipped")
          out << "      <skipped/>\n";
        else if (c->status == "failed")
          out << "      <failure message=\"" << xml_escape(c->message.c_str())
              << "\"/>\n";
        else
          out << "      <error message=\""
              << xml_escape(c->message.empty() ? "incomplete"
                                               : c->message.c_str())
              << "\"/>\n";
        out << "    </testcase>\n";
      }
      out << "  </testsuite>\n";
    }
    out << "</testsuites>\n";
  }

  void start_case(const doctest::TestCaseData &in) {
    RecordedCase c;
    c.suite = suite_of(in);
    c.name = in.m_name ? in.m_name : "";
    c.status = "started";
    cases_.push_back(std::move(c));
    subcases_.clear();
    pending_message_.clear();
    pending_error_ = false;
    flush();
  }

public:
  explicit JunitFileListener(const doctest::ContextOptions &) {
    const char *path = std::getenv("YONA_DOCTEST_JUNIT");
    if (path && path[0] != '\0')
      path_ = path;
  }

  void report_query(const doctest::QueryData &) override {}
  void test_run_start() override {}
  void test_run_end(const doctest::TestRunStats &) override { flush(); }

  void test_case_start(const doctest::TestCaseData &in) override {
    start_case(in);
  }

  void test_case_reenter(const doctest::TestCaseData &in) override {
    if (!cases_.empty() && cases_.back().status == "started") {
      for (const auto &sub : subcases_) {
        if (!sub.empty())
          cases_.back().name += "/" + sub;
      }
      cases_.back().status = "passed";
    }
    start_case(in);
  }

  void test_case_end(const doctest::CurrentTestCaseStats &st) override {
    if (cases_.empty())
      return;
    auto &c = cases_.back();
    for (const auto &sub : subcases_) {
      if (!sub.empty())
        c.name += "/" + sub;
    }
    subcases_.clear();
    c.seconds = st.seconds;
    if (st.failure_flags & doctest::TestCaseFailureReason::Crash) {
      c.status = "error";
      if (c.message.empty())
        c.message = pending_message_.empty() ? "crash" : pending_message_;
    } else if (pending_error_ ||
               (st.failure_flags & doctest::TestCaseFailureReason::Exception)) {
      c.status = "error";
      if (c.message.empty())
        c.message = pending_message_.empty() ? "exception" : pending_message_;
    } else if (!st.testCaseSuccess || st.numAssertsFailedCurrentTest > 0 ||
               (st.failure_flags &
                doctest::TestCaseFailureReason::AssertFailure)) {
      c.status = "failed";
      if (c.message.empty())
        c.message = pending_message_;
    } else {
      c.status = "passed";
    }
    flush();
  }

  void test_case_exception(const doctest::TestCaseException &e) override {
    pending_error_ = true;
    if (e.error_string.c_str())
      pending_message_ = e.error_string.c_str();
    if (e.is_crash && pending_message_.empty())
      pending_message_ = "crash";
  }

  void subcase_start(const doctest::SubcaseSignature &in) override {
    subcases_.push_back(in.m_name.c_str() ? in.m_name.c_str() : "");
  }
  void subcase_end() override {}

  void log_assert(const doctest::AssertData &a) override {
    if (!a.m_failed)
      return;
    if (a.m_decomp.c_str())
      pending_message_ = a.m_decomp.c_str();
  }
  void log_message(const doctest::MessageData &) override {}

  void test_case_skipped(const doctest::TestCaseData &in) override {
    RecordedCase c;
    c.suite = suite_of(in);
    c.name = in.m_name ? in.m_name : "";
    c.status = "skipped";
    cases_.push_back(std::move(c));
    flush();
  }
};

} // namespace

REGISTER_LISTENER("yona_junit", 1, JunitFileListener);

int main(int argc, char **argv) {
  doctest::Context context;

  // Apply command line arguments
  context.applyCommandLine(argc, argv);

  // Run tests
  int res = context.run();

  return res;
}
