# Unit Tests for ReXGlue core components

add_executable(unit_tests
    audio/audio_system_test.cpp
    memory/heap_allocation_test.cpp
    memory/ppc_memory_compat_test.cpp
    kernel/object_table_test.cpp
    kernel/ldi_decompression_test.cpp
    kernel/xboxkrnl_audio_test.cpp
    kernel/xboxkrnl_device_request_test.cpp
    kernel/xboxkrnl_memory_test.cpp
    kernel/xboxkrnl_modules_test.cpp
    kernel/xboxkrnl_ob_test.cpp
    kernel/xboxkrnl_video_test.cpp
    kernel/xboxkrnl_xconfig_test.cpp
    kernel/xboxkrnl_io_stub_test.cpp
    kernel/xboxkrnl_usbcam_test.cpp
    core/cvar_test.cpp
    core/perf_counter_test.cpp
    core/fiber_test.cpp
    core/platform_exceptions_test.cpp
    core/sha256_test.cpp
    core/stream_test.cpp
    core/byte_order_test.cpp
    core/chrono_test.cpp
    filesystem/virtual_file_system_test.cpp
    graphics/draw_diagnostics_test.cpp
    graphics/pipeline_util_test.cpp
    kernel/xboxkrnl_rtl_test.cpp
    kernel/xboxkrnl_crypt_test.cpp
    kernel/xam_task_test.cpp
    kernel/xam_content_test.cpp
    kernel/xam_misc_test.cpp
    kernel/xam_avatar_test.cpp
    kernel/xam_net_test.cpp
    kernel/xam_user_test.cpp
    kernel/xam_xmp_test.cpp
    kernel/xam_ui_test.cpp
    kernel/xam_voice_test.cpp
    codegen/config_test.cpp
    codegen/codegen_context_test.cpp
    codegen/function_graph_test.cpp
    codegen/function_node_emit_test.cpp
    codegen/function_pointer_discovery_test.cpp
    codegen/jump_table_discovery_test.cpp
    codegen/template_registry_test.cpp
    ppc/import_function_test.cpp
    ppc/stack_test.cpp
    system/guest_path_test.cpp
    system/function_dispatcher_test.cpp
    system/xthread_test.cpp
    ui/d3d12_messages_test.cpp
    codegen/manifest_test.cpp
    rexglue/codegen_command_test.cpp
    rexglue/cmake_version_test.cpp
    rexglue/init_command_test.cpp
    rexglue/migration_scan_test.cpp
    rexglue/ui_test.cpp
    ui/rex_app_test.cpp
)

target_sources(unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rexglue/commands/legacy_config.cpp
    ${PROJECT_SOURCE_DIR}/src/rexglue/commands/migration_scan.cpp
    ${PROJECT_SOURCE_DIR}/src/rexglue/ui/ui.cpp
    ${PROJECT_SOURCE_DIR}/src/rexglue/ui/progress.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/rex_app.cpp
)

target_include_directories(unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/thirdparty/cli11/include
)
if(TARGET imgui)
    target_include_directories(unit_tests PRIVATE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

target_link_libraries(unit_tests PRIVATE
    rexcore
    rexcodegen
    rexruntime
    Catch2::Catch2WithMain
)

target_compile_definitions(unit_tests PRIVATE
    REXGLUE_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_executable(sdk_import_smoke
    core/perf_counter_sdk_import_smoke.cpp
)
target_link_libraries(sdk_import_smoke PRIVATE
    rexruntime
)
set_target_properties(sdk_import_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:rexruntime>
)
add_dependencies(unit_tests sdk_import_smoke)
add_test(NAME unit.sdk_import_smoke COMMAND sdk_import_smoke)

# Strip Tracy/profiling instrumentation from test targets
target_compile_options(unit_tests PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING
    -UREXGLUE_ENABLE_PERF_COUNTERS -UREXGLUE_PROFILE_GUEST_FUNCTIONS
)

set_target_properties(unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:rexruntime>
)
if(UNIX AND NOT APPLE)
    set_target_properties(unit_tests PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH ON
    )
endif()

# Discover tests
include(Catch)
catch_discover_tests(unit_tests
    TEST_PREFIX "unit."
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60
)

add_executable(runtime_unit_tests
    audio/xma_decoder_runtime_test.cpp
    graphics/texture_cache_test.cpp
    kernel/xboxkrnl_io_runtime_test.cpp
    kernel/xboxkrnl_memory_runtime_test.cpp
    kernel/rtl_raise_exception_test.cpp
    kernel/xboxkrnl_ob_runtime_test.cpp
    kernel/xam_info_test.cpp
    kernel/xam_ui_runtime_test.cpp
    kernel/xam_user_runtime_test.cpp
    input/sdl_input_driver_runtime_test.cpp
)

target_include_directories(runtime_unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/thirdparty/xxHash
)

target_link_libraries(runtime_unit_tests PRIVATE
    rexruntime
    Catch2::Catch2WithMain
)

target_compile_options(runtime_unit_tests PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING
    -UREXGLUE_ENABLE_PERF_COUNTERS -UREXGLUE_PROFILE_GUEST_FUNCTIONS
)

set_target_properties(runtime_unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:rexruntime>
)

catch_discover_tests(runtime_unit_tests
    TEST_PREFIX "runtime."
    PROPERTIES
        LABELS "unit;runtime"
        TIMEOUT 60
)
