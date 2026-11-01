# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(test_l2_counters test_l2_counters.c)
target_link_libraries(test_l2_counters PRIVATE l2_counters)
add_dependencies(tests test_l2_counters)
add_test(NAME test_l2_counters COMMAND test_l2_counters)
