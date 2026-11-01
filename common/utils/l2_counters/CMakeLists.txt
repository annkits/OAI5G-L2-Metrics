# SPDX-License-Identifier: LicenseRef-CSSL-1.0

# config/LOG symbols used by l2_counters_config.c come from the softmodem
add_library(l2_counters STATIC
            l2_counters.c
            l2_counters_json.c
            l2_counters_config.c)
target_include_directories(l2_counters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(l2_counters PRIVATE log_headers pthread)

if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()
