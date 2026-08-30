#include "yona/Interface/Module.h"

#include "yona/Model/ModuleIdentity.h"

#include <string>
#include <utility>

namespace yona::interface {

std::string Function::exportName(const model::ModuleIdentity &Module) const {
  return Module.mangle(Name);
}

ExportReference::ExportReference(model::ModuleIdentity Module,
                                 std::string LocalName)
    : Module(std::move(Module)), LocalName(std::move(LocalName)) {}

std::string ExportReference::exportName() const {
  return Module.mangle(LocalName);
}

TraitImplementation::TraitImplementation(std::string MethodName,
                                         ExportReference Target)
    : MethodName(std::move(MethodName)), Target(std::move(Target)) {}

GenericDependency::GenericDependency(Function Contract, DependencyTarget Target)
    : Contract(std::move(Contract)), Target(std::move(Target)) {}

InterfaceModule::InterfaceModule(model::ModuleIdentity Identity)
    : Identity(std::move(Identity)) {}

} // namespace yona::interface
