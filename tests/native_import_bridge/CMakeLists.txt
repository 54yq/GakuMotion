cmake_minimum_required(VERSION 3.20)
project(GakuMotionNativeImportBridgeTests LANGUAGES CXX)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_executable(native_import_bridge_tests ../test_native_import_bridge.cpp)
target_compile_features(native_import_bridge_tests PRIVATE cxx_std_20)
target_include_directories(native_import_bridge_tests PRIVATE "${REPO}/src" "${REPO}/include" "${REPO}/deps/KineTexture/src")
target_compile_definitions(native_import_bridge_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(native_import_bridge_tests PRIVATE /W4 /utf-8)
add_library(old_abi SHARED fake_importer.cpp)
target_compile_definitions(old_abi PRIVATE BRIDGE_FAKE_ABI=2 BRIDGE_FAKE_RETURN=0)
add_library(bad_status SHARED fake_importer.cpp)
target_compile_definitions(bad_status PRIVATE BRIDGE_FAKE_ABI=3 BRIDGE_FAKE_RETURN=3)
foreach(target old_abi bad_status)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "GakuMotion.VmdImporter.Native"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${target}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/${target}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${target}"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/${target}")
endforeach()
