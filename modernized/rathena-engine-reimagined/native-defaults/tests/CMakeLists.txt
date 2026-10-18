add_executable(native_defaults_tests main.cpp test_numeric_and_effects.cpp test_registry.cpp
  test_damage_stages.cpp test_mob_ai.cpp test_differential.cpp)
target_link_libraries(native_defaults_tests PRIVATE native_defaults)
target_compile_options(native_defaults_tests PRIVATE -Wall -Wextra)

add_test(NAME all COMMAND native_defaults_tests)
foreach(rule BR-RULESET-111 BR-RULESET-113 BR-RULESET-115 BR-RULESET-116 BR-RULESET-118 BR-RULESET-119)
  add_test(NAME ${rule} COMMAND native_defaults_tests --tag ${rule})
endforeach()
