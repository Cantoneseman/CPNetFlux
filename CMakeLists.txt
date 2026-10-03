cmake_minimum_required(VERSION 3.20)

project(
    CPNetFlux
    VERSION 0.1.0
    DESCRIPTION "GridFTP-compatible high-performance transfer foundation"
    LANGUAGES CXX
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "CPNetFlux is Linux-only.")
endif()

option(CPNETFLUX_BUILD_TESTS "Build CPNetFlux tests" ON)
option(CPNETFLUX_ENABLE_IO_URING "Enable optional file-IO-only io_uring backend" OFF)
option(CPNETFLUX_ENABLE_TLS "Enable optional OpenSSL control-plane TLS alpha support when available" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(spdlog CONFIG QUIET)
if(CPNETFLUX_BUILD_TESTS)
    find_package(GTest CONFIG QUIET)
endif()

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)

if(NOT spdlog_FOUND)
    FetchContent_MakeAvailable(spdlog)
endif()

set(CPNETFLUX_CRC32C_HW_SOURCE src/checksum/crc32c_hw_stub.cpp)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86"
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CPNETFLUX_CRC32C_HW_SOURCE src/checksum/crc32c_hw_x86.cpp)
    set_source_files_properties(src/checksum/crc32c_hw_x86.cpp
        PROPERTIES
            COMPILE_OPTIONS "-msse4.2"
    )
endif()

set(CPNETFLUX_FILE_IO_URING_SOURCE src/storage/file_io_uring_stub.cpp)
set(CPNETFLUX_HAS_IO_URING 0)
if(CPNETFLUX_ENABLE_IO_URING)
    find_path(CPNETFLUX_LIBURING_INCLUDE_DIR NAMES liburing.h)
    find_library(CPNETFLUX_LIBURING_LIBRARY NAMES uring)
    if(CPNETFLUX_LIBURING_INCLUDE_DIR AND CPNETFLUX_LIBURING_LIBRARY)
        set(CPNETFLUX_FILE_IO_URING_SOURCE src/storage/file_io_uring.cpp)
        set(CPNETFLUX_HAS_IO_URING 1)
        message(STATUS "CPNetFlux io_uring backend enabled")
    else()
        message(WARNING "CPNETFLUX_ENABLE_IO_URING=ON but liburing was not found; building unavailable backend stub")
    endif()
endif()

set(CPNETFLUX_TLS_SOURCE src/core/io/tls_socket_stub.cpp)
set(CPNETFLUX_HAS_TLS 0)
if(CPNETFLUX_ENABLE_TLS)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(CPNETFLUX_TLS_SOURCE src/core/io/tls_socket.cpp)
        set(CPNETFLUX_HAS_TLS 1)
        message(STATUS "CPNetFlux control-plane TLS alpha enabled with OpenSSL")
    else()
        message(WARNING "OpenSSL development files were not found; building TLS unavailable stub")
    endif()
endif()

add_library(cpnetflux_core STATIC
    src/checkpoint/download_manifest.cpp
    src/checkpoint/manifest_store.cpp
    src/checkpoint/transfer_manifest.cpp
    src/checksum/checksum.cpp
    src/checksum/crc32c.cpp
    ${CPNETFLUX_CRC32C_HW_SOURCE}
    src/common/throughput_counter.cpp
    src/common/version.cpp
    src/config/file_transfer_options.cpp
    src/config/file_download_options.cpp
    src/config/tree_transfer_options.cpp
    src/config/sink_options.cpp
    src/core/chunk/chunk_planner.cpp
    src/core/chunk/range_list.cpp
    src/core/chunk/transfer_progress.cpp
    src/core/io/connection_context.cpp
    src/core/io/file_download_client.cpp
    src/core/io/file_download_sender.cpp
    src/core/io/file_transfer_client.cpp
    src/core/io/file_transfer_server.cpp
    src/core/io/framed_data_socket.cpp
    src/core/io/socket_utils.cpp
    src/core/io/tcp_sink_client.cpp
    src/core/io/tcp_sink_server.cpp
    ${CPNETFLUX_TLS_SOURCE}
    src/core/io/tree_transfer_client.cpp
    src/core/metrics/error_code.cpp
    src/core/metrics/event_log.cpp
    src/core/metrics/transfer_phase_stats.cpp
    src/core/protocol/compressed_data.cpp
    src/core/protocol/frame.cpp
    src/core/scheduler/compression_advisor.cpp
    src/core/scheduler/feedback_controller.cpp
    src/core/scheduler/link_manager.cpp
    src/core/scheduler/metrics_writer.cpp
    src/core/scheduler/scheduler.cpp
    src/core/scheduler/task_queue.cpp
    src/core/scheduler/work_item_planner.cpp
    src/core/session/download_session.cpp
    src/core/session/commit_sync_policy.cpp
    src/core/session/final_verify_policy.cpp
    src/core/session/manifest_flush_policy.cpp
    src/core/session/transfer_session.cpp
    src/core/tree/tree_manifest.cpp
    src/core/tree/tree_scan.cpp
    src/protocol/control/control_command.cpp
    src/protocol/control/control_auth.cpp
    src/protocol/control/control_options.cpp
    src/protocol/control/control_server.cpp
    src/storage/file_io.cpp
    ${CPNETFLUX_FILE_IO_URING_SOURCE}
    src/storage/posix_file.cpp
    src/storage/preallocate_mode.cpp
)

add_library(CPNetFlux::core ALIAS cpnetflux_core)

target_include_directories(cpnetflux_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(cpnetflux_core
    PUBLIC
        Threads::Threads
        spdlog::spdlog
    PRIVATE
        ZLIB::ZLIB
)

target_compile_definitions(cpnetflux_core
    PUBLIC
        CPNETFLUX_HAS_IO_URING=${CPNETFLUX_HAS_IO_URING}
        CPNETFLUX_HAS_TLS=${CPNETFLUX_HAS_TLS}
)

if(CPNETFLUX_HAS_IO_URING)
    target_include_directories(cpnetflux_core
        PRIVATE
            ${CPNETFLUX_LIBURING_INCLUDE_DIR}
    )
    target_link_libraries(cpnetflux_core
        PRIVATE
            ${CPNETFLUX_LIBURING_LIBRARY}
    )
endif()

if(CPNETFLUX_HAS_TLS)
    target_link_libraries(cpnetflux_core
        PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
    )
endif()

target_compile_features(cpnetflux_core PUBLIC cxx_std_20)

add_executable(cpnetflux
    src/main.cpp
)

target_link_libraries(cpnetflux
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-server
    src/apps/cpnetflux_server.cpp
)

target_link_libraries(cpnetflux-server
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-client
    src/apps/cpnetflux_client.cpp
)

target_link_libraries(cpnetflux-client
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-file-server
    src/apps/cpnetflux_file_server.cpp
)

target_link_libraries(cpnetflux-file-server
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-file-client
    src/apps/cpnetflux_file_client.cpp
)

target_link_libraries(cpnetflux-file-client
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-file-download-client
    src/apps/cpnetflux_file_download_client.cpp
)

target_link_libraries(cpnetflux-file-download-client
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-tree-upload-client
    src/apps/cpnetflux_tree_upload_client.cpp
)

target_link_libraries(cpnetflux-tree-upload-client
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-tree-download-client
    src/apps/cpnetflux_tree_download_client.cpp
)

target_link_libraries(cpnetflux-tree-download-client
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-checksum-bench
    src/apps/cpnetflux_checksum_bench.cpp
)

target_link_libraries(cpnetflux-checksum-bench
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-storage-bench
    src/apps/cpnetflux_storage_bench.cpp
)

target_link_libraries(cpnetflux-storage-bench
    PRIVATE
        CPNetFlux::core
)

add_executable(cpnetflux-gridftp-server
    src/apps/cpnetflux_gridftp_server.cpp
)

target_link_libraries(cpnetflux-gridftp-server
    PRIVATE
        CPNetFlux::core
)

if(CPNETFLUX_BUILD_TESTS)
    enable_testing()
    if(NOT GTest_FOUND)
        FetchContent_MakeAvailable(googletest)
    endif()
    include(GoogleTest)

    add_executable(cpnetflux_unit_tests
        tests/unit/checksum_test.cpp
        tests/unit/chunk_planner_test.cpp
        tests/unit/connection_context_test.cpp
        tests/unit/control_command_test.cpp
        tests/unit/control_options_test.cpp
        tests/unit/control_session_test.cpp
        tests/unit/compressed_data_test.cpp
        tests/unit/download_manifest_test.cpp
        tests/unit/download_session_test.cpp
        tests/unit/event_log_test.cpp
        tests/unit/file_download_options_test.cpp
        tests/unit/file_io_test.cpp
        tests/unit/file_transfer_options_test.cpp
        tests/unit/frame_test.cpp
        tests/unit/hot_path_compression_test.cpp
        tests/unit/manifest_store_test.cpp
        tests/unit/posix_file_test.cpp
        tests/unit/range_list_test.cpp
        tests/unit/scheduler_compression_advisor_test.cpp
        tests/unit/scheduler_feedback_controller_test.cpp
        tests/unit/scheduler_link_manager_test.cpp
        tests/unit/scheduler_metrics_writer_test.cpp
        tests/unit/scheduler_task_queue_test.cpp
        tests/unit/scheduler_work_item_test.cpp
        tests/unit/sink_options_test.cpp
        tests/unit/throughput_counter_test.cpp
        tests/unit/transfer_manifest_test.cpp
        tests/unit/transfer_session_test.cpp
        tests/unit/transfer_progress_test.cpp
        tests/unit/tree_manifest_test.cpp
        tests/unit/tree_scan_test.cpp
        tests/unit/tree_transfer_options_test.cpp
        tests/unit/tree_pipeline_state_test.cpp
        tests/unit/tree_pipeline_identity_test.cpp
        tests/unit/version_test.cpp
    )

    target_link_libraries(cpnetflux_unit_tests
        PRIVATE
            CPNetFlux::core
            GTest::gtest_main
    )

    gtest_discover_tests(cpnetflux_unit_tests)

    add_test(
        NAME cpnetflux_file_transfer_smoke
        COMMAND bash ${PROJECT_SOURCE_DIR}/tools/test/run_file_transfer_smoke.sh --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_file_resume_smoke
        COMMAND bash ${PROJECT_SOURCE_DIR}/tools/test/run_file_resume_smoke.sh --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_file_checksum_smoke
        COMMAND bash ${PROJECT_SOURCE_DIR}/tools/test/run_file_checksum_smoke.sh --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_stor_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_stor_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_resume_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_resume_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_retr_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_retr_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_retr_resume_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_retr_resume_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_retr_corrupt_resume_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_retr_corrupt_resume_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_metadata_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_metadata_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_list_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_list_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_token_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_token_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_control_tls_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_control_tls_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_gridftp_data_tls_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_data_tls_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_event_log_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_event_log_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_upload_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_upload_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_download_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_download_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_resume_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_resume_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_parallel_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_parallel_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_control_reuse_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_control_reuse_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_pipeline_sidecar_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_pipeline_sidecar_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_changed_file_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_changed_file_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_scheduler_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_scheduler_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_edge_cases_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_edge_cases_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_tree_manifest_corrupt_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_gridftp_tree_manifest_corrupt_smoke.py --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME cpnetflux_release_hygiene
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/release/test_public_hygiene.py
    )
    add_test(
        NAME cpnetflux_alpha_release_helper_behavior
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/release/test_alpha_release_helpers.py
    )
    add_test(
        NAME cpnetflux_alpha_artifact_sync_behavior
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/release/test_alpha_release_helpers.py
    )
    add_test(
        NAME cpnetflux_alpha_demo_helper_behavior
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/demo/test_alpha_demo.py
    )
    add_test(
        NAME cpnetflux_alpha_soak_smoke
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/run_alpha_soak_smoke.py --build-dir ${CMAKE_BINARY_DIR} --iterations 1 --results-dir ${CMAKE_BINARY_DIR}/alpha-soak-smoke
    )
    add_test(
        NAME cpnetflux_alpha_soak_helper_behavior
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/test/test_alpha_soak_smoke.py
    )
    add_test(
        NAME cpnetflux_storage_bench_wrapper_behavior
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/benchmark/test_run_storage_bench.py --script ${PROJECT_SOURCE_DIR}/tools/benchmark/run_storage_bench.py
    )
endif()
