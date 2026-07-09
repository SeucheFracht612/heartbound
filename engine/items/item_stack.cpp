#include "engine/items/item_stack.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::items {

core::Status ItemDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("item_definition.invalid_prototype",
                                     "item definition prototype id must be valid");
    }
    if (stack_limit == 0) {
        return core::Status::failure("item_definition.invalid_stack_limit",
                                     "item definition stack limit must be non-zero");
    }
    for (const auto& tag : tags) {
        if (!core::is_valid_local_id(tag)) {
            return core::Status::failure("item_definition.invalid_tag",
                                         "item definition tag is invalid: " + tag);
        }
    }
    return core::Status::ok();
}

core::Result<ItemStack> ItemDefinition::create_stack(std::uint32_t count,
                                                     std::uint16_t quality) const {
    auto status = validate();
    if (!status) {
        return core::Result<ItemStack>::failure(status.error().code, status.error().message);
    }
    return ItemStack::create(prototype_id, count, stack_limit, quality);
}

core::Result<ItemStack> ItemStack::create(core::PrototypeId prototype_id, std::uint32_t count,
                                          std::uint32_t max_count, std::uint16_t quality) {
    if (!prototype_id.is_valid()) {
        return core::Result<ItemStack>::failure("item.invalid_prototype",
                                                "item stack prototype id must be valid");
    }
    if (max_count == 0) {
        return core::Result<ItemStack>::failure("item.invalid_max_count",
                                                "item stack max count must be non-zero");
    }
    if (count == 0 || count > max_count) {
        return core::Result<ItemStack>::failure("item.invalid_count",
                                                "item stack count must be between 1 and max count");
    }

    return core::Result<ItemStack>::success(
        ItemStack{std::move(prototype_id), count, max_count, quality});
}

bool ItemStack::is_empty() const noexcept {
    return count == 0;
}

std::uint32_t ItemStack::remaining_capacity() const noexcept {
    return count >= max_count ? 0 : max_count - count;
}

bool ItemStack::can_merge_with(const ItemStack& other) const noexcept {
    return prototype_id == other.prototype_id && quality == other.quality &&
           max_count == other.max_count;
}

core::Status ItemStack::add_from(ItemStack& source, std::uint32_t requested_count) {
    if (!can_merge_with(source)) {
        return core::Status::failure("item.merge_mismatch",
                                     "item stacks must share prototype, quality, and max count");
    }
    if (requested_count == 0) {
        return core::Status::failure("item.invalid_transfer_count",
                                     "item transfer count must be non-zero");
    }

    const auto moved = std::min({requested_count, source.count, remaining_capacity()});
    if (moved == 0) {
        return core::Status::failure("item.stack_full", "destination item stack is full");
    }

    count += moved;
    source.count -= moved;
    return core::Status::ok();
}

core::Result<ItemStack> ItemStack::split(std::uint32_t requested_count) {
    if (requested_count == 0 || requested_count > count) {
        return core::Result<ItemStack>::failure("item.invalid_split_count",
                                                "split count must be between 1 and stack count");
    }

    count -= requested_count;
    return core::Result<ItemStack>::success(
        ItemStack{prototype_id, requested_count, max_count, quality});
}

} // namespace heartstead::items
