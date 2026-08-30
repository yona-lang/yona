#ifndef YONA_MODEL_MODULEIDENTITY_H
#define YONA_MODEL_MODULEIDENTITY_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace yona::model {

/// Canonical identity of a Yona module.
///
/// Module identities have no field mutators and are safe to read concurrently.
/// Copy or move assignment still requires exclusive access and invalidates
/// references into the assigned object. Construction rejects empty, relative,
/// and non-PascalCase segments by throwing `std::invalid_argument`. The object
/// owns every segment and every string returned by value is independently owned
/// by the caller.
class ModuleIdentity final {
public:
  explicit ModuleIdentity(std::string_view Name);

  /// Borrow canonical segments until assignment or object destruction.
  [[nodiscard]] const std::vector<std::string> &segments() const noexcept {
    return Segments;
  }

  [[nodiscard]] std::string fqn() const;
  [[nodiscard]] std::filesystem::path relativePath() const;

  /// Return the sole public C export spelling for Symbol.
  ///
  /// Symbol must be a non-empty camelCase Yona name or a compiler-generated
  /// trait symbol composed of identifier segments separated by underscores.
  /// Invalid symbols throw `std::invalid_argument`. The operation has no
  /// shared mutable state and is thread-safe.
  [[nodiscard]] std::string mangle(std::string_view Symbol) const;

private:
  std::vector<std::string> Segments;
};

/// Parse ModuleName and produce its canonical public C export spelling.
/// Throws `std::invalid_argument` on malformed module or symbol names.
[[nodiscard]] std::string mangleExport(std::string_view ModuleName,
                                       std::string_view Symbol);

} // namespace yona::model

#endif /* YONA_MODEL_MODULEIDENTITY_H */
