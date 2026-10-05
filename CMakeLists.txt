cmake_minimum_required(VERSION 3.20)
project(bms_core LANGUAGES C)

# This target deliberately has no MCU/AFE/SDK dependency. Firmware products
# compile these same files with TC32; a future STM32 target can link this library.
add_library(bms_core STATIC
    bms/core/storage_record.c
    bms/core/bms_soc_eta.c
    bms/core/bms_state.c
    bms/core/bms_debug_log.c
    bms/core/bms_diag.c
    bms/core/bms_sw_protection.c
    bms/core/bms_crc.c)
target_include_directories(bms_core PUBLIC bms/core)
target_compile_features(bms_core PUBLIC c_std_99)
if(MSVC)
    target_compile_options(bms_core PRIVATE /W4 /WX)
else()
    target_compile_options(bms_core PRIVATE -Wall -Wextra -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(storage_record_host_test tests/storage_record_host_test.c)
    target_link_libraries(storage_record_host_test PRIVATE bms_core)
    add_test(NAME storage_journal COMMAND storage_record_host_test)
    add_executable(bms_core_host_test tests/bms_core_host_test.c)
    target_link_libraries(bms_core_host_test PRIVATE bms_core)
    add_test(NAME portable_core COMMAND bms_core_host_test)
    add_executable(bms_soc_eta_host_test tests/bms_soc_eta_host_test.c)
    target_link_libraries(bms_soc_eta_host_test PRIVATE bms_core)
    add_test(NAME soc_eta COMMAND bms_soc_eta_host_test)
    # These tests contain executable assertions, including with Release builds.
    foreach(test_target storage_record_host_test bms_core_host_test bms_soc_eta_host_test)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /W4 /WX /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        endif()
    endforeach()
endif()
