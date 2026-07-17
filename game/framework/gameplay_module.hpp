#pragma once

#include "engine/core/result.hpp"
#include "engine/entities/entity_world.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/net/server_command.hpp"
#include "engine/simulation/simulation_scheduler.hpp"
#include "game/framework/domain_service_registry.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace heartstead::game {

struct ComponentRegistration {
    std::string name;
    std::type_index type = typeid(void);
};

class ComponentRegistry final {
  public:
    template <typename Component> [[nodiscard]] core::Status register_component(std::string name) {
        return register_type(std::move(name), typeid(Component));
    }

    [[nodiscard]] std::span<const ComponentRegistration> registrations() const noexcept;

  private:
    [[nodiscard]] core::Status register_type(std::string name, std::type_index type);
    std::vector<ComponentRegistration> registrations_;
};

struct VersionedGameplayRegistration {
    std::string name;
    std::uint32_t version = 1;
};

class SerializationRegistry final {
  public:
    [[nodiscard]] core::Status register_schema(VersionedGameplayRegistration registration);
    [[nodiscard]] std::span<const VersionedGameplayRegistration> registrations() const noexcept;

  private:
    std::vector<VersionedGameplayRegistration> registrations_;
};

struct ReplicationRegistration {
    std::string name;
    std::uint32_t version = 1;
    bool reliable = true;
};

class ReplicationRegistry final {
  public:
    [[nodiscard]] core::Status register_replication(ReplicationRegistration registration);
    [[nodiscard]] std::span<const ReplicationRegistration> registrations() const noexcept;

  private:
    std::vector<ReplicationRegistration> registrations_;
};

struct PresentationRegistration {
    std::string name;
    std::uint32_t version = 1;
};

class PresentationRegistry final {
  public:
    [[nodiscard]] core::Status register_adapter(PresentationRegistration registration);
    [[nodiscard]] std::span<const PresentationRegistration> registrations() const noexcept;

  private:
    std::vector<PresentationRegistration> registrations_;
};

struct GameplayRegistrationContext {
    const modding::PrototypeRegistry& content;
    entities::EntityWorld& entities;
    net::ServerCommandDispatcher& commands;
    simulation::SimulationScheduler& scheduler;
    ComponentRegistry& components;
    SerializationRegistry& serializers;
    ReplicationRegistry& replication;
    PresentationRegistry& presentation;
    DomainServiceRegistry& services;
};

class IGameplayModule {
  public:
    virtual ~IGameplayModule() = default;
    [[nodiscard]] virtual std::string_view module_id() const noexcept = 0;
    [[nodiscard]] virtual core::Status
    validate_content(const modding::PrototypeRegistry& content) const;
    [[nodiscard]] virtual core::Status register_components(ComponentRegistry& registry);
    [[nodiscard]] virtual core::Status register_services(DomainServiceRegistry& registry);
    [[nodiscard]] virtual core::Status register_commands(GameplayRegistrationContext& context);
    [[nodiscard]] virtual core::Status register_systems(GameplayRegistrationContext& context);
    [[nodiscard]] virtual core::Status register_serializers(SerializationRegistry& registry);
    [[nodiscard]] virtual core::Status register_replication(ReplicationRegistry& registry);
    [[nodiscard]] virtual core::Status register_presentation(PresentationRegistry& registry);
};

struct GameplayModuleRegistrationReport {
    std::vector<std::string> module_ids;
    std::size_t component_count = 0;
    std::size_t service_count = 0;
    std::size_t command_count = 0;
    std::size_t system_count = 0;
    std::size_t serializer_count = 0;
    std::size_t replication_count = 0;
    std::size_t presentation_adapter_count = 0;
};

class GameplayModuleRegistry final {
  public:
    [[nodiscard]] core::Status add(std::shared_ptr<IGameplayModule> module);
    [[nodiscard]] core::Result<GameplayModuleRegistrationReport>
    register_all(GameplayRegistrationContext& context);

    [[nodiscard]] std::span<const std::shared_ptr<IGameplayModule>> modules() const noexcept;
    [[nodiscard]] const GameplayModuleRegistrationReport& report() const noexcept;

  private:
    std::vector<std::shared_ptr<IGameplayModule>> modules_;
    GameplayModuleRegistrationReport report_;
    bool registered_ = false;
};

} // namespace heartstead::game
