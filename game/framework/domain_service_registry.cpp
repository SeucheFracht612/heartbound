#include "game/framework/domain_service_registry.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace heartstead::game {

core::Status DomainServiceRegistry::register_erased(std::string name, std::type_index type,
                                                    std::shared_ptr<void> service) {
    if (name.empty() || type == typeid(void) || service == nullptr) {
        return core::Status::failure(
            "domain_service.invalid_registration",
            "domain service requires a name, concrete interface type, and implementation");
    }
    if (services_.contains(type) ||
        std::ranges::any_of(registrations_, [&name](const auto& registration) {
            return registration.name == name;
        })) {
        return core::Status::failure("domain_service.duplicate_registration",
                                     "domain service name or interface is already registered: " +
                                         name);
    }
    services_.emplace(type, std::move(service));
    registrations_.push_back({std::move(name), type});
    return core::Status::ok();
}

std::span<const DomainServiceRegistration>
DomainServiceRegistry::registrations() const noexcept {
    return registrations_;
}

} // namespace heartstead::game
