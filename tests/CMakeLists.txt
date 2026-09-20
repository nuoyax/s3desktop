# Unit tests.
#
# The suites link only s3desktop_core, so they run without a display. The original
# project had no tests at all, which is why every regression had to be found by
# hand against a live bucket.

find_package(Qt6 REQUIRED COMPONENTS Test)

set(S3DESKTOP_TEST_SOURCES
    test_sigv4.cpp
    test_list_parser.cpp
    test_target_profile.cpp
    test_transfer_queue.cpp
)

foreach(test_source IN LISTS S3DESKTOP_TEST_SOURCES)
    get_filename_component(test_name ${test_source} NAME_WE)

    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE s3desktop_core Qt6::Test Qt6::Network)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Qt places the moc output for a test next to its source, so the include
    # directory above is what makes "#include "x.moc"" resolve.
    set_target_properties(${test_name} PROPERTIES AUTOMOC ON)

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# The object model is widget-adjacent rather than core, so it links the GUI
# library and runs under QApplication.
add_executable(test_object_model test_object_model.cpp)
target_link_libraries(test_object_model PRIVATE s3desktop_ui Qt6::Test)
target_include_directories(test_object_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_object_model PROPERTIES AUTOMOC ON)
add_test(NAME test_object_model COMMAND test_object_model)

# The browser's navigation: back, forward, up, and the breadcrumb.
add_executable(test_object_browser test_object_browser.cpp)
target_link_libraries(test_object_browser PRIVATE s3desktop_ui Qt6::Test)
target_include_directories(test_object_browser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_object_browser PROPERTIES AUTOMOC ON)
add_test(NAME test_object_browser COMMAND test_object_browser)
