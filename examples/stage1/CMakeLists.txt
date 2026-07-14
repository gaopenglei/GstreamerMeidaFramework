add_library(stage1_common STATIC stage1_common.c)
target_include_directories(stage1_common PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GST_INCLUDE_DIRS}
)
target_compile_options(stage1_common PRIVATE ${GST_CFLAGS_OTHER})
target_link_libraries(stage1_common PUBLIC ${GST_LIBRARIES})

function(add_stage1_lesson target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GST_INCLUDE_DIRS}
    )
    target_compile_options(${target} PRIVATE ${GST_CFLAGS_OTHER})
    target_link_libraries(${target} PRIVATE stage1_common)

    if(BUILD_TESTS)
        add_test(NAME ${target} COMMAND ${target})
    endif()
endfunction()

add_stage1_lesson(stage1_01_elements 01_elements.c)
add_stage1_lesson(stage1_02_pipeline 02_pipeline.c)
add_stage1_lesson(stage1_03_bus 03_bus.c)
add_stage1_lesson(stage1_04_dynamic_pad 04_dynamic_pad.c)
add_stage1_lesson(stage1_05_caps 05_caps.c)
add_stage1_lesson(stage1_06_request_pad 06_request_pad.c)
