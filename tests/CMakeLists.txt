find_package(GTest CONFIG REQUIRED)

add_executable(
  ts4_common_tests
  test_CrashDumpRules.cpp
  test_GameVersion.cpp
  test_Hotkey.cpp
  test_ModCore.cpp
  test_ModHost.cpp
  test_SettingsFields.cpp
  test_SettingsFile.cpp
  test_Signature.cpp
  test_StartupGuard.cpp
  test_Result.cpp
)
target_link_libraries(
  ts4_common_tests PRIVATE ts4_common ts4_mod_core ts4_mod_sdk nlohmann_json::nlohmann_json GTest::gtest_main
)
# The tests of the Windows parts use the test exe itself: its sections, its functions and its hooks.
if (WIN32)
  target_sources(ts4_common_tests PRIVATE test_Hooks.cpp test_Module.cpp test_Scan.cpp)
  target_link_libraries(ts4_common_tests PRIVATE ts4_mod_game)
endif ()
ts4_set_warnings(ts4_common_tests)

include(GoogleTest)
gtest_discover_tests(ts4_common_tests DISCOVERY_MODE PRE_TEST)
