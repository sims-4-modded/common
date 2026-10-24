find_package(GTest CONFIG REQUIRED)

add_executable(ts4_common_tests test_GameVersion.cpp test_ModHost.cpp test_Result.cpp)
target_link_libraries(ts4_common_tests PRIVATE ts4_common ts4_mod_core ts4_mod_sdk GTest::gtest_main)
ts4_set_warnings(ts4_common_tests)

include(GoogleTest)
gtest_discover_tests(ts4_common_tests DISCOVERY_MODE PRE_TEST)
