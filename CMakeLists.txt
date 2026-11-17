cmake_minimum_required(VERSION 3.20)
project(f2400 VERSION 0.2.0 LANGUAGES C)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "SG_IO acquisition requires Linux")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(TIFF REQUIRED IMPORTED_TARGET libtiff-4)
pkg_check_modules(JSON REQUIRED IMPORTED_TARGET json-c)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(F2400_SOURCES
    src/device.c
    src/util.c
    src/transport.c
    src/capture.c
    src/initialization.c
    src/exposure.c
    src/curves.c
    src/calibration.c
    src/image.c
    src/audit.c
    src/pipeline.c
)
add_library(f2400_objects OBJECT ${F2400_SOURCES})
set_target_properties(f2400_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
)
target_include_directories(f2400_objects PUBLIC include PRIVATE src)
target_compile_definitions(f2400_objects PUBLIC _GNU_SOURCE)
target_compile_options(f2400_objects PRIVATE
    -Wall -Wextra -Werror -ffp-contract=off -fno-fast-math
)
target_link_libraries(f2400_objects PUBLIC
    PkgConfig::PNG PkgConfig::TIFF PkgConfig::JSON OpenSSL::Crypto m
)

add_library(f2400_core SHARED $<TARGET_OBJECTS:f2400_objects>)
set_target_properties(f2400_core PROPERTIES
    OUTPUT_NAME f2400 VERSION ${PROJECT_VERSION} SOVERSION 0
)
target_include_directories(f2400_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(f2400_core PRIVATE f2400_objects)

add_executable(f2400 src/cli.c)
target_include_directories(f2400 PRIVATE src)
target_compile_options(f2400 PRIVATE -Wall -Wextra -Werror)
target_link_libraries(f2400 PRIVATE f2400_objects)

include(GNUInstallDirs)
install(TARGETS f2400 f2400_core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ui/ DESTINATION ${CMAKE_INSTALL_DATADIR}/f2400/ui
    FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE
)
install(FILES udev/70-f2400.rules DESTINATION ${CMAKE_INSTALL_DATADIR}/f2400)
install(FILES README.md BUILD.md ORIGINAL_GAPS.md NOTICE.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
configure_file(ui/f2400-ui.in ${CMAKE_CURRENT_BINARY_DIR}/f2400-ui @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/f2400-ui
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(CTest)
option(F2400_TEST_UI "Run optional Tk UI tests (requires an available display)" OFF)
if(BUILD_TESTING)
    find_package(ZLIB REQUIRED)
    function(add_core_test name)
        add_executable(test_${name} tests/test_${name}.c)
        target_include_directories(test_${name} PRIVATE src)
        target_link_libraries(test_${name} PRIVATE f2400_objects)
        target_compile_options(test_${name} PRIVATE
            -UNDEBUG -Wall -Wextra -Werror -ffp-contract=off -fno-fast-math
        )
    endfunction()
    add_core_test(core)
    add_test(NAME core_original_parity COMMAND test_core
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures ${CMAKE_CURRENT_BINARY_DIR}
    )
    foreach(test capture calibration_transport initialization)
        add_core_test(${test})
        target_link_options(test_${test} PRIVATE -Wl,--wrap=ioctl)
    endforeach()
    target_link_libraries(test_initialization PRIVATE ZLIB::ZLIB)
    target_link_options(test_initialization PRIVATE -Wl,--wrap=usleep)
    add_test(NAME capture_faults COMMAND test_capture)
    add_test(NAME calibration_faults COMMAND test_calibration_transport)
    foreach(block 38 39)
        add_test(NAME cold_recorded_${block} COMMAND test_initialization
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cold-block${block}.bin.gz
            0 0 ${block}
        )
    endforeach()
    foreach(mode RANGE 1 3)
        foreach(point 10 1975)
            add_test(NAME cold_failure_${point}_${mode} COMMAND test_initialization
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cold-block39.bin.gz
                ${point} ${mode}
            )
        endforeach()
    endforeach()
    foreach(test device errors)
        add_executable(test_${test} tests/test_${test}.c)
        target_include_directories(test_${test} PRIVATE src)
        target_compile_definitions(test_${test} PRIVATE _GNU_SOURCE)
        target_compile_options(test_${test} PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    endforeach()
    add_test(NAME scanner_discovery COMMAND test_device)
    add_test(NAME original_error_decoder COMMAND test_errors
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/errors.tsv
    )
    add_core_test(jobs)
    target_link_options(test_jobs PRIVATE
        -Wl,--wrap=ioctl -Wl,--wrap=f2400_device_open -Wl,--wrap=flock -Wl,--wrap=close
    )
    add_test(NAME native_jobs COMMAND test_jobs ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
    if(F2400_TEST_UI)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME desktop_ui
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui.py
        )
        set_tests_properties(desktop_ui PROPERTIES ENVIRONMENT
            "F2400_CLI=$<TARGET_FILE:f2400>;F2400_LIBRARY=$<TARGET_FILE:f2400_core>"
        )
    endif()
endif()
