#include "engine/renderer/benchmark/benchmark_scene.hpp"
#include "engine/renderer/benchmark/benchmark_statistics.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <string>

namespace {

void test_benchmark_statistics() {
    using namespace heartstead::renderer;
    benchmark::BenchmarkRecorder recorder("deterministic-test", 77);
    constexpr std::array frame_times{1.0, 2.0, 3.0, 100.0};
    for (std::size_t index = 0; index < frame_times.size(); ++index) {
        RendererStats stats;
        stats.frame_index = index;
        stats.cpu_frame_ms = frame_times[index];
        stats.meshing_ms = static_cast<double>(index);
        stats.upload_ms = 0.5;
        stats.gpu_wait_ms = 0.25;
        stats.uploaded_bytes_this_frame = 16;
        if (index >= 2) {
            stats.gpu_timing_valid = true;
            stats.gpu_frame_ms = static_cast<double>(index);
        }
        recorder.record(stats);
    }

    const auto summary = recorder.summarize();
    assert(summary.scene == "deterministic-test");
    assert(summary.seed == 77);
    assert(summary.sample_count == 4);
    assert(summary.gpu_sample_count == 2);
    assert(std::abs(summary.median_frame_ms - 2.5) < 0.0001);
    assert(std::abs(summary.p95_frame_ms - 85.45) < 0.0001);
    assert(std::abs(summary.p99_frame_ms - 97.09) < 0.0001);
    assert(std::abs(summary.one_percent_low_fps - 10.0) < 0.0001);
    assert(std::abs(summary.point_one_percent_low_fps - 10.0) < 0.0001);
    assert(summary.maximum_frame_ms == 100.0);
    assert(summary.total_uploaded_bytes == 64);
    assert(recorder.to_json().find("\"p99_frame_ms\": 97.090000") != std::string::npos);
    assert(recorder.to_json().find("\"frames\": [") != std::string::npos);
    assert(recorder.to_csv().find("scene,seed,frame,cpu_frame_ms") == 0);
    assert(benchmark::format_benchmark_summary(summary).find("0.1%low=10.000fps") !=
           std::string::npos);
}

void test_all_deterministic_scenes_construct() {
    using namespace heartstead::renderer::benchmark;
    constexpr std::array kinds{
        BenchmarkSceneKind::flat_terrain,
        BenchmarkSceneKind::mountainous_terrain,
        BenchmarkSceneKind::dense_caves,
        BenchmarkSceneKind::checkerboard_geometry,
        BenchmarkSceneKind::forest_cross_planes,
        BenchmarkSceneKind::rapid_voxel_edits,
        BenchmarkSceneKind::high_speed_flythrough,
        BenchmarkSceneKind::chunk_load_unload_churn,
        BenchmarkSceneKind::large_coordinates,
        BenchmarkSceneKind::resize_minimize_stress,
    };
    for (const auto kind : kinds) {
        BenchmarkSceneConfig config;
        config.kind = kind;
        config.seed = 1234;
        config.chunk_radius = 0;
        auto scene = BenchmarkScene::create(config);
        assert(scene);
        assert(scene.value()->world().chunks().chunk_count() == 1);
        assert(scene.value()->palette().size() == 4);
        assert(scene.value()->camera().view_projection.is_finite());
        assert(parse_benchmark_scene(benchmark_scene_name(kind)) == kind);
    }
    assert(!parse_benchmark_scene("not-a-scene"));
}

void test_scene_content_is_reproducible() {
    using namespace heartstead;
    renderer::benchmark::BenchmarkSceneConfig config;
    config.kind = renderer::benchmark::BenchmarkSceneKind::dense_caves;
    config.seed = 0x12345678;
    config.chunk_radius = 0;
    auto first = renderer::benchmark::BenchmarkScene::create(config);
    auto second = renderer::benchmark::BenchmarkScene::create(config);
    assert(first && second);
    const auto* first_chunk = first.value()->world().chunks().records().front();
    const auto* second_chunk = second.value()->world().chunks().records().front();
    for (std::uint16_t z = 0; z < world::VoxelChunk::edge_length; z += 5) {
        for (std::uint16_t y = 0; y < world::VoxelChunk::edge_length; y += 3) {
            for (std::uint16_t x = 0; x < world::VoxelChunk::edge_length; x += 7) {
                const auto first_cell = first_chunk->get({x, y, z});
                const auto second_cell = second_chunk->get({x, y, z});
                assert(first_cell && second_cell);
                assert(first_cell.value() == second_cell.value());
            }
        }
    }
}

void test_dynamic_scene_schedules() {
    using namespace heartstead::renderer::benchmark;

    BenchmarkSceneConfig edit_config;
    edit_config.kind = BenchmarkSceneKind::rapid_voxel_edits;
    edit_config.chunk_radius = 0;
    auto edits = BenchmarkScene::create(edit_config);
    assert(edits);
    auto* edit_chunk = edits.value()->world().chunks().records().front();
    const auto old_revision = edit_chunk->content_revision();
    assert(edits.value()->advance(1));
    assert(edit_chunk->content_revision() > old_revision);

    BenchmarkSceneConfig churn_config;
    churn_config.kind = BenchmarkSceneKind::chunk_load_unload_churn;
    churn_config.chunk_radius = 0;
    auto churn = BenchmarkScene::create(churn_config);
    assert(churn);
    const auto old_identity = churn.value()->world().chunks().identities().front();
    assert(churn.value()->advance(0));
    const auto new_identity = churn.value()->world().chunks().identities().front();
    assert(new_identity.coordinate == old_identity.coordinate);
    assert(new_identity.load_generation > old_identity.load_generation);

    BenchmarkSceneConfig fly_config;
    fly_config.kind = BenchmarkSceneKind::large_coordinates;
    fly_config.chunk_radius = 0;
    auto flythrough = BenchmarkScene::create(fly_config);
    assert(flythrough);
    assert(flythrough.value()->advance(99));
    assert(flythrough.value()->camera().floating_origin.block.x > 30'000'000'000LL);
    assert(std::abs(flythrough.value()->camera().local_position.x) < 1.0F);

    BenchmarkSceneConfig resize_config;
    resize_config.kind = BenchmarkSceneKind::resize_minimize_stress;
    resize_config.chunk_radius = 0;
    auto resize = BenchmarkScene::create(resize_config);
    assert(resize);
    const auto minimized = resize.value()->advance(0);
    assert(minimized && minimized.value().skip_render);
    assert(minimized.value().requested_extent.has_value());
    assert(!minimized.value().requested_extent->is_valid());
    const auto restored = resize.value()->advance(1);
    assert(restored && !restored.value().skip_render);
    assert(restored.value().requested_extent.has_value());
    assert(restored.value().requested_extent->width == 800);
    assert(restored.value().requested_extent->height == 600);
}

} // namespace

int main() {
    test_benchmark_statistics();
    test_all_deterministic_scenes_construct();
    test_scene_content_is_reproducible();
    test_dynamic_scene_schedules();
    return 0;
}
