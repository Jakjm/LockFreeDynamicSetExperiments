# CMake generated Testfile for 
# Source directory: /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7
# Build directory: /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test-api "/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/mimalloc-test-api")
set_tests_properties(test-api PROPERTIES  _BACKTRACE_TRIPLES "/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeLists.txt;576;add_test;/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeLists.txt;0;")
add_test(test-api-fill "/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/mimalloc-test-api-fill")
set_tests_properties(test-api-fill PROPERTIES  _BACKTRACE_TRIPLES "/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeLists.txt;576;add_test;/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeLists.txt;0;")
add_test(test-stress "/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/mimalloc-test-stress")
set_tests_properties(test-stress PROPERTIES  _BACKTRACE_TRIPLES "/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeLists.txt;576;add_test;/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeLists.txt;0;")
