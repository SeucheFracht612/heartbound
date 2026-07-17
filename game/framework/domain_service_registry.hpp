#pragma once

#include "engine/core/result.hpp"

#include <memory>
#include <span>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace heartstead::game {

struct DomainServiceRegistration {
    std::string name;
    std::type_index type = typeid(void);
};

class DomainServiceRegistry final {
  public:
    template <typename Service>
    [[nodiscard]] core::Status register_service(std::string name,
                                                std::shared_ptr<Service> service) {
        if (service == nullptr) {
            return core::Status::failure("domain_service.null_service",
                                         "domain service registration cannot be null");
        }
        return register_erased(std::move(name), typeid(Service), std::move(service));
    }

    template <typename Service> [[nodiscard]] Service* find() noexcept {
        const auto found = services_.find(std::type_index(typeid(Service)));
        return found == services_.end() ? nullptr : static_cast<Service*>(found->second.get());
    }

    template <typename Service> [[nodiscard]] const Service* find() const noexcept {
        const auto found = services_.find(std::type_index(typeid(Service)));
        return found == services_.end() ? nullptr
                                        : static_cast<const Service*>(found->second.get());
    }

    [[nodiscard]] std::span<const DomainServiceRegistration> registrations() const noexcept;

  private:
    [[nodiscard]] core::Status register_erased(std::string name, std::type_index type,
                                               std::shared_ptr<void> service);

    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
    std::vector<DomainServiceRegistration> registrations_;
};

} // namespace heartstead::game
