# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.28

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7

# Include any dependencies generated for this target.
include CMakeFiles/mimalloc-test-stress.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/mimalloc-test-stress.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/mimalloc-test-stress.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/mimalloc-test-stress.dir/flags.make

CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o: CMakeFiles/mimalloc-test-stress.dir/flags.make
CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o: test/test-stress.c
CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o: CMakeFiles/mimalloc-test-stress.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o"
	/usr/bin/gcc-13 $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o -MF CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o.d -o CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o -c /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/test/test-stress.c

CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.i"
	/usr/bin/gcc-13 $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/test/test-stress.c > CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.i

CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.s"
	/usr/bin/gcc-13 $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/test/test-stress.c -o CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.s

# Object files for target mimalloc-test-stress
mimalloc__test__stress_OBJECTS = \
"CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o"

# External object files for target mimalloc-test-stress
mimalloc__test__stress_EXTERNAL_OBJECTS =

mimalloc-test-stress: CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o
mimalloc-test-stress: CMakeFiles/mimalloc-test-stress.dir/build.make
mimalloc-test-stress: libmimalloc.so.2.1
mimalloc-test-stress: CMakeFiles/mimalloc-test-stress.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable mimalloc-test-stress"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/mimalloc-test-stress.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/mimalloc-test-stress.dir/build: mimalloc-test-stress
.PHONY : CMakeFiles/mimalloc-test-stress.dir/build

CMakeFiles/mimalloc-test-stress.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/mimalloc-test-stress.dir/cmake_clean.cmake
.PHONY : CMakeFiles/mimalloc-test-stress.dir/clean

CMakeFiles/mimalloc-test-stress.dir/depend:
	cd /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7 && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7 /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7 /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7 /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7 /home/jakj3m/LockFreeDynamicSetExperiments/mimalloc-2.1.7/CMakeFiles/mimalloc-test-stress.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/mimalloc-test-stress.dir/depend

