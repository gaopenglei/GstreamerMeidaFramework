add_executable(test_stage2_core test_stage2_core.c)
target_include_directories(test_stage2_core PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GST_INCLUDE_DIRS}
)
target_compile_options(test_stage2_core PRIVATE ${GST_CFLAGS_OTHER})
target_link_libraries(test_stage2_core PRIVATE
    media_framework_lib
    ${GST_LIBRARIES}
)

add_test(NAME test_stage2_core COMMAND test_stage2_core)
set_tests_properties(test_stage2_core PROPERTIES TIMEOUT 60)
