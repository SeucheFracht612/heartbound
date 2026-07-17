#include "engine/renderer/rhi/render_frame_plan.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>
#include <thread>
#include <vector>

namespace {

template <typename Outcome> void expect_wrong_thread(const Outcome& outcome) {
    assert(!outcome);
    assert(outcome.error().code == "renderer.render_device_wrong_thread");
}

void test_render_device_mutations_are_owner_thread_only() {
    using namespace heartstead::renderer::rhi;

    auto device_result = create_render_device({});
    assert(device_result);
    auto& device = device_result.value();

    const auto buffer = device->create_buffer(
        {RenderBufferUsage::storage, 16, "owner_thread_probe", RenderBufferMemory::host_visible});
    assert(buffer);
    const auto initial_extent = device->current_extent();
    const auto initial_live_resources = device->live_resource_count();
    const auto initial_frame_count = device->completed_frame_count();
    const auto initial_submission_serial = device->last_submission_serial();

    std::thread worker([&] {
        constexpr std::array<std::byte, 4> bytes{};
        constexpr std::array<std::uint32_t, 5> spirv_words{
            0x07230203,
            0x00010000,
            0,
            1,
            0,
        };
        const std::array<RenderBufferWrite, 1> buffer_writes{{
            {buffer.value().handle, 0, std::span<const std::byte>(bytes)},
        }};
        const std::vector<RenderDescriptorWrite> descriptor_writes;
        const std::vector<RenderMeshBinding> mesh_draws;
        const auto plan = make_clear_present_frame_plan({320, 180}, {}, false);

        expect_wrong_thread(device->resize({320, 180}));
        expect_wrong_thread(device->render_frame({{}, {320, 180}, false}));
        expect_wrong_thread(device->execute_frame_plan(plan));
        expect_wrong_thread(device->execute_frame({plan, {}, {}, {}}));
        expect_wrong_thread(device->create_buffer(
            {RenderBufferUsage::storage, bytes.size(), "wrong_thread_create"}));
        expect_wrong_thread(device->upload_buffer_batch(buffer_writes));
        expect_wrong_thread(device->upload_buffer(
            {RenderBufferUsage::storage, bytes.size(), "wrong_thread_upload"}, bytes));
        expect_wrong_thread(device->upload_image(
            {RenderImageFormat::rgba8_unorm, 1, 1, "wrong_thread_image"}, bytes));
        expect_wrong_thread(device->create_sampler({}));
        expect_wrong_thread(device->create_shader_module(
            {RenderShaderStage::vertex, "wrong_thread_shader"}, spirv_words));
        expect_wrong_thread(device->bind_pipeline_layout({}));
        expect_wrong_thread(device->create_compute_pipeline({}));
        expect_wrong_thread(device->create_graphics_pipeline({}));
        expect_wrong_thread(device->write_descriptors(descriptor_writes));
        expect_wrong_thread(device->bind_mesh_draws(mesh_draws));
        expect_wrong_thread(device->release_resource(buffer.value().handle));
    });
    worker.join();

    assert(device->current_extent().width == initial_extent.width);
    assert(device->current_extent().height == initial_extent.height);
    assert(device->live_resource_count() == initial_live_resources);
    assert(device->completed_frame_count() == initial_frame_count);
    assert(device->last_submission_serial() == initial_submission_serial);
    assert(device->release_resource(buffer.value().handle));
}

} // namespace

int main() {
    test_render_device_mutations_are_owner_thread_only();
    return 0;
}
