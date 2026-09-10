# Test classification is independent of availability. Runners must report an
# unavailable GPU explicitly rather than remove those registrations.
get_property(phase0_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${phase0_tests} PROPERTIES LABELS "host" TIMEOUT 600)
set(phase0_gpu_tests
    shader_recompiler_compute shader_recompiler_alignbyte command_scheduler_timeline
    stream_buffer_ring gpu_command_lane gpu_tiler texture_cache_layered_image
    texture_cache_image_views texture_cache_storage_sampled texture_cache_depth_readback
    buffer_cache_dirty_gc pm4_context_state compute_meta_clear_classification phase0_wave_mask phase0_eop_visibility)
if(WIN32)
    list(APPEND phase0_gpu_tests texture_cache_image_overlap texture_cache_htile_clear buffer_cache_ranges)
endif()
set_tests_properties(${phase0_gpu_tests} PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
set_tests_properties(shader_recompiler_compute shader_recompiler_alignbyte phase0_wave_mask
    PROPERTIES LABELS "gpu;spirv")
set_tests_properties(phase0_spirv_validation PROPERTIES LABELS "host;spirv")
set_tests_properties(virtual_memory_allocation guest_red_zone_patcher phase0_unresolved_import
    PROPERTIES LABELS "host;native")
set_tests_properties(phase0_unresolved_import phase0_path_containment phase0_wave_mask phase0_eop_visibility
    PROPERTIES TIMEOUT 60)
set_tests_properties(phase1_imports phase1_tls phase1_native_state phase1_patching
    phase1_load_rollback phase1_modules PROPERTIES LABELS "host;native" TIMEOUT 60)
set_tests_properties(phase1_paths phase1_executables PROPERTIES TIMEOUT 60)
set_tests_properties(phase2_kernel phase2_kernel_stress PROPERTIES LABELS "host;native" TIMEOUT 120)
set_tests_properties(phase3_completion phase3_ownership phase3_flip_boundary phase3_packet_bounds PROPERTIES LABELS "gpu;phase3" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(phase4a_wave_contracts phase4a_wave_regressions phase4a_wave_contracts_32 phase4a_wave_regressions_32 PROPERTIES LABELS "gpu;spirv;phase4a" RUN_SERIAL TRUE TIMEOUT 180)
