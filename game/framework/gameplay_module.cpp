#include "game/framework/gameplay_module.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::game {

namespace {

template <typename Registration>
[[nodiscard]] core::Status validate_versioned_registration(
    const Registration& registration, std::span<const Registration> existing,
    std::string_view kind) {
    if (registration.name.empty() || registration.version == 0) {
        return core::Status::failure("gameplay_module.invalid_registration",
                                     std::string(kind) +
                                         " registration requires a name and non-zero version");
    }
    if (std::ranges::any_of(existing, [&registration](const auto& value) {
            return value.name == registration.name;
        })) {
        return core::Status::failure("gameplay_module.duplicate_registration",
                                     std::string(kind) + " registration is duplicated: " +
                                         registration.name);
    }
    return core::Status::ok();
}

} // namespace

core::Status ComponentRegistry::register_type(std::string name, std::type_index type) {
    if (name.empty() || type == typeid(void)) {
        return core::Status::failure(
            "gameplay_module.invalid_component",
            "component registration requires a name and a concrete component type");
    }
    if (std::ranges::any_of(registrations_, [&name, type](const auto& registration) {
            return registration.name == name || registration.type == type;
        })) {
        return core::Status::failure("gameplay_module.duplicate_component",
                                     "component name or type is already registered: " + name);
    }
    registrations_.push_back({std::move(name), type});
    return core::Status::ok();
}

std::span<const ComponentRegistration> ComponentRegistry::registrations() const noexcept {
    return registrations_;
}

core::Status
SerializationRegistry::register_schema(VersionedGameplayRegistration registration) {
    auto status = validate_versioned_registration(
        registration, std::span<const VersionedGameplayRegistration>(registrations_),
        "serializer");
    if (!status) {
        return status;
    }
    registrations_.push_back(std::move(registration));
    return core::Status::ok();
}

std::span<const VersionedGameplayRegistration>
SerializationRegistry::registrations() const noexcept {
    return registrations_;
}

core::Status ReplicationRegistry::register_replication(ReplicationRegistration registration) {
    auto status = validate_versioned_registration(
        registration, std::span<const ReplicationRegistration>(registrations_), "replication");
    if (!status) {
        return status;
    }
    registrations_.push_back(std::move(registration));
    return core::Status::ok();
}

std::span<const ReplicationRegistration> ReplicationRegistry::registrations() const noexcept {
    return registrations_;
}

core::Status PresentationRegistry::register_adapter(PresentationRegistration registration) {
    auto status = validate_versioned_registration(
        registration, std::span<const PresentationRegistration>(registrations_), "presentation");
    if (!status) {
        return status;
    }
    registrations_.push_back(std::move(registration));
    return core::Status::ok();
}

std::span<const PresentationRegistration> PresentationRegistry::registrations() const noexcept {
    return registrations_;
}

core::Status
IGameplayModule::validate_content(const modding::PrototypeRegistry&) const {
    return core::Status::ok();
}

core::Status IGameplayModule::register_components(ComponentRegistry&) {
    return core::Status::ok();
}

core::Status IGameplayModule::register_services(DomainServiceRegistry&) {
    return core::Status::ok();
}

core::Status IGameplayModule::register_commands(GameplayRegistrationContext&) {
    return core::Status::ok();
}

core::Status IGameplayModule::register_systems(GameplayRegistrationContext&) {
    return core::Status::ok();
}

core::Status IGameplayModule::register_serializers(SerializationRegistry&) {
    return core::Status::ok();
}

core::Status IGameplayModule::register_replication(ReplicationRegistry&) {
    return core::Status::ok();
}

core::Status IGameplayModule::register_presentation(PresentationRegistry&) {
    return core::Status::ok();
}

core::Status GameplayModuleRegistry::add(std::shared_ptr<IGameplayModule> module) {
    if (registered_) {
        return core::Status::failure("gameplay_module.registry_finalized",
                                     "gameplay modules cannot be added after registration");
    }
    if (module == nullptr || module->module_id().empty()) {
        return core::Status::failure("gameplay_module.invalid_module",
                                     "gameplay module and module id must be valid");
    }
    if (std::ranges::any_of(modules_, [&module](const auto& existing) {
            return existing->module_id() == module->module_id();
        })) {
        return core::Status::failure("gameplay_module.duplicate_module",
                                     "gameplay module id is duplicated: " +
                                         std::string(module->module_id()));
    }
    modules_.push_back(std::move(module));
    return core::Status::ok();
}

core::Result<GameplayModuleRegistrationReport>
GameplayModuleRegistry::register_all(GameplayRegistrationContext& context) {
    if (registered_) {
        return core::Result<GameplayModuleRegistrationReport>::failure(
            "gameplay_module.registry_finalized", "gameplay modules are already registered");
    }
    const auto command_count_before = context.commands.size();
    const auto system_count_before = context.scheduler.registered_system_count();
    for (const auto& module : modules_) {
        auto status = module->validate_content(context.content);
        if (status) {
            status = module->register_components(context.components);
        }
        if (status) {
            status = module->register_services(context.services);
        }
        if (status) {
            status = module->register_commands(context);
        }
        if (status) {
            status = module->register_systems(context);
        }
        if (status) {
            status = module->register_serializers(context.serializers);
        }
        if (status) {
            status = module->register_replication(context.replication);
        }
        if (status) {
            status = module->register_presentation(context.presentation);
        }
        if (!status) {
            return core::Result<GameplayModuleRegistrationReport>::failure(
                status.error().code,
                "gameplay module '" + std::string(module->module_id()) +
                    "' registration failed: " + status.error().message);
        }
        report_.module_ids.emplace_back(module->module_id());
    }
    report_.component_count = context.components.registrations().size();
    report_.service_count = context.services.registrations().size();
    report_.command_count = context.commands.size() - command_count_before;
    const auto system_count_after = context.scheduler.registered_system_count();
    report_.system_count = system_count_after >= system_count_before
                               ? system_count_after - system_count_before
                               : 0;
    report_.serializer_count = context.serializers.registrations().size();
    report_.replication_count = context.replication.registrations().size();
    report_.presentation_adapter_count = context.presentation.registrations().size();
    registered_ = true;
    return core::Result<GameplayModuleRegistrationReport>::success(report_);
}

std::span<const std::shared_ptr<IGameplayModule>>
GameplayModuleRegistry::modules() const noexcept {
    return modules_;
}

const GameplayModuleRegistrationReport& GameplayModuleRegistry::report() const noexcept {
    return report_;
}

} // namespace heartstead::game
