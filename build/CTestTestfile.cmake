# CMake generated Testfile for 
# Source directory: /home/carver/Dimage Scan Dual/f2400-driver
# Build directory: /home/carver/Dimage Scan Dual/f2400-driver/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("core_original_parity" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_core" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures" "/home/carver/Dimage Scan Dual/f2400-driver/build")
set_tests_properties("core_original_parity" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;90;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("capture_faults" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_capture")
set_tests_properties("capture_faults" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;99;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("calibration_faults" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_calibration_transport")
set_tests_properties("calibration_faults" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;100;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_recorded_38" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block38.bin.gz" "0" "0" "38")
set_tests_properties("cold_recorded_38" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;102;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_recorded_39" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block39.bin.gz" "0" "0" "39")
set_tests_properties("cold_recorded_39" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;102;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_failure_10_1" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block39.bin.gz" "10" "1")
set_tests_properties("cold_failure_10_1" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;109;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_failure_1975_1" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block39.bin.gz" "1975" "1")
set_tests_properties("cold_failure_1975_1" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;109;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_failure_10_2" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block39.bin.gz" "10" "2")
set_tests_properties("cold_failure_10_2" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;109;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_failure_1975_2" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block39.bin.gz" "1975" "2")
set_tests_properties("cold_failure_1975_2" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;109;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_failure_10_3" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block39.bin.gz" "10" "3")
set_tests_properties("cold_failure_10_3" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;109;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("cold_failure_1975_3" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_initialization" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/cold-block39.bin.gz" "1975" "3")
set_tests_properties("cold_failure_1975_3" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;109;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("scanner_discovery" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_device")
set_tests_properties("scanner_discovery" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;121;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("original_error_decoder" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_errors" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures/errors.tsv")
set_tests_properties("original_error_decoder" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;122;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
add_test("native_jobs" "/home/carver/Dimage Scan Dual/f2400-driver/build/test_jobs" "/home/carver/Dimage Scan Dual/f2400-driver/tests/fixtures")
set_tests_properties("native_jobs" PROPERTIES  _BACKTRACE_TRIPLES "/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;129;add_test;/home/carver/Dimage Scan Dual/f2400-driver/CMakeLists.txt;0;")
