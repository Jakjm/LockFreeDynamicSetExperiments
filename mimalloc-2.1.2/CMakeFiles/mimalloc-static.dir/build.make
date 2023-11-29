# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.27

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
CMAKE_SOURCE_DIR = /home/jakjm/Downloads/mimalloc-2.1.2

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/jakjm/Downloads/mimalloc-2.1.2

# Include any dependencies generated for this target.
include CMakeFiles/mimalloc-static.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/mimalloc-static.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/mimalloc-static.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/mimalloc-static.dir/flags.make

CMakeFiles/mimalloc-static.dir/src/alloc.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/alloc.c.o: src/alloc.c
CMakeFiles/mimalloc-static.dir/src/alloc.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/mimalloc-static.dir/src/alloc.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/alloc.c.o -MF CMakeFiles/mimalloc-static.dir/src/alloc.c.o.d -o CMakeFiles/mimalloc-static.dir/src/alloc.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc.c

CMakeFiles/mimalloc-static.dir/src/alloc.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/alloc.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc.c > CMakeFiles/mimalloc-static.dir/src/alloc.c.i

CMakeFiles/mimalloc-static.dir/src/alloc.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/alloc.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc.c -o CMakeFiles/mimalloc-static.dir/src/alloc.c.s

CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o: src/alloc-aligned.c
CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building C object CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o -MF CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o.d -o CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc-aligned.c

CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc-aligned.c > CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.i

CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc-aligned.c -o CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.s

CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o: src/alloc-posix.c
CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building C object CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o -MF CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o.d -o CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc-posix.c

CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc-posix.c > CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.i

CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/alloc-posix.c -o CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.s

CMakeFiles/mimalloc-static.dir/src/arena.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/arena.c.o: src/arena.c
CMakeFiles/mimalloc-static.dir/src/arena.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Building C object CMakeFiles/mimalloc-static.dir/src/arena.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/arena.c.o -MF CMakeFiles/mimalloc-static.dir/src/arena.c.o.d -o CMakeFiles/mimalloc-static.dir/src/arena.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/arena.c

CMakeFiles/mimalloc-static.dir/src/arena.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/arena.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/arena.c > CMakeFiles/mimalloc-static.dir/src/arena.c.i

CMakeFiles/mimalloc-static.dir/src/arena.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/arena.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/arena.c -o CMakeFiles/mimalloc-static.dir/src/arena.c.s

CMakeFiles/mimalloc-static.dir/src/bitmap.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/bitmap.c.o: src/bitmap.c
CMakeFiles/mimalloc-static.dir/src/bitmap.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_5) "Building C object CMakeFiles/mimalloc-static.dir/src/bitmap.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/bitmap.c.o -MF CMakeFiles/mimalloc-static.dir/src/bitmap.c.o.d -o CMakeFiles/mimalloc-static.dir/src/bitmap.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/bitmap.c

CMakeFiles/mimalloc-static.dir/src/bitmap.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/bitmap.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/bitmap.c > CMakeFiles/mimalloc-static.dir/src/bitmap.c.i

CMakeFiles/mimalloc-static.dir/src/bitmap.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/bitmap.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/bitmap.c -o CMakeFiles/mimalloc-static.dir/src/bitmap.c.s

CMakeFiles/mimalloc-static.dir/src/heap.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/heap.c.o: src/heap.c
CMakeFiles/mimalloc-static.dir/src/heap.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_6) "Building C object CMakeFiles/mimalloc-static.dir/src/heap.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/heap.c.o -MF CMakeFiles/mimalloc-static.dir/src/heap.c.o.d -o CMakeFiles/mimalloc-static.dir/src/heap.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/heap.c

CMakeFiles/mimalloc-static.dir/src/heap.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/heap.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/heap.c > CMakeFiles/mimalloc-static.dir/src/heap.c.i

CMakeFiles/mimalloc-static.dir/src/heap.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/heap.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/heap.c -o CMakeFiles/mimalloc-static.dir/src/heap.c.s

CMakeFiles/mimalloc-static.dir/src/init.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/init.c.o: src/init.c
CMakeFiles/mimalloc-static.dir/src/init.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_7) "Building C object CMakeFiles/mimalloc-static.dir/src/init.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/init.c.o -MF CMakeFiles/mimalloc-static.dir/src/init.c.o.d -o CMakeFiles/mimalloc-static.dir/src/init.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/init.c

CMakeFiles/mimalloc-static.dir/src/init.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/init.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/init.c > CMakeFiles/mimalloc-static.dir/src/init.c.i

CMakeFiles/mimalloc-static.dir/src/init.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/init.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/init.c -o CMakeFiles/mimalloc-static.dir/src/init.c.s

CMakeFiles/mimalloc-static.dir/src/options.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/options.c.o: src/options.c
CMakeFiles/mimalloc-static.dir/src/options.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_8) "Building C object CMakeFiles/mimalloc-static.dir/src/options.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/options.c.o -MF CMakeFiles/mimalloc-static.dir/src/options.c.o.d -o CMakeFiles/mimalloc-static.dir/src/options.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/options.c

CMakeFiles/mimalloc-static.dir/src/options.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/options.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/options.c > CMakeFiles/mimalloc-static.dir/src/options.c.i

CMakeFiles/mimalloc-static.dir/src/options.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/options.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/options.c -o CMakeFiles/mimalloc-static.dir/src/options.c.s

CMakeFiles/mimalloc-static.dir/src/os.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/os.c.o: src/os.c
CMakeFiles/mimalloc-static.dir/src/os.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_9) "Building C object CMakeFiles/mimalloc-static.dir/src/os.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/os.c.o -MF CMakeFiles/mimalloc-static.dir/src/os.c.o.d -o CMakeFiles/mimalloc-static.dir/src/os.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/os.c

CMakeFiles/mimalloc-static.dir/src/os.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/os.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/os.c > CMakeFiles/mimalloc-static.dir/src/os.c.i

CMakeFiles/mimalloc-static.dir/src/os.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/os.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/os.c -o CMakeFiles/mimalloc-static.dir/src/os.c.s

CMakeFiles/mimalloc-static.dir/src/page.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/page.c.o: src/page.c
CMakeFiles/mimalloc-static.dir/src/page.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_10) "Building C object CMakeFiles/mimalloc-static.dir/src/page.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/page.c.o -MF CMakeFiles/mimalloc-static.dir/src/page.c.o.d -o CMakeFiles/mimalloc-static.dir/src/page.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/page.c

CMakeFiles/mimalloc-static.dir/src/page.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/page.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/page.c > CMakeFiles/mimalloc-static.dir/src/page.c.i

CMakeFiles/mimalloc-static.dir/src/page.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/page.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/page.c -o CMakeFiles/mimalloc-static.dir/src/page.c.s

CMakeFiles/mimalloc-static.dir/src/random.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/random.c.o: src/random.c
CMakeFiles/mimalloc-static.dir/src/random.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_11) "Building C object CMakeFiles/mimalloc-static.dir/src/random.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/random.c.o -MF CMakeFiles/mimalloc-static.dir/src/random.c.o.d -o CMakeFiles/mimalloc-static.dir/src/random.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/random.c

CMakeFiles/mimalloc-static.dir/src/random.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/random.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/random.c > CMakeFiles/mimalloc-static.dir/src/random.c.i

CMakeFiles/mimalloc-static.dir/src/random.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/random.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/random.c -o CMakeFiles/mimalloc-static.dir/src/random.c.s

CMakeFiles/mimalloc-static.dir/src/segment.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/segment.c.o: src/segment.c
CMakeFiles/mimalloc-static.dir/src/segment.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_12) "Building C object CMakeFiles/mimalloc-static.dir/src/segment.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/segment.c.o -MF CMakeFiles/mimalloc-static.dir/src/segment.c.o.d -o CMakeFiles/mimalloc-static.dir/src/segment.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/segment.c

CMakeFiles/mimalloc-static.dir/src/segment.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/segment.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/segment.c > CMakeFiles/mimalloc-static.dir/src/segment.c.i

CMakeFiles/mimalloc-static.dir/src/segment.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/segment.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/segment.c -o CMakeFiles/mimalloc-static.dir/src/segment.c.s

CMakeFiles/mimalloc-static.dir/src/segment-map.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/segment-map.c.o: src/segment-map.c
CMakeFiles/mimalloc-static.dir/src/segment-map.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_13) "Building C object CMakeFiles/mimalloc-static.dir/src/segment-map.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/segment-map.c.o -MF CMakeFiles/mimalloc-static.dir/src/segment-map.c.o.d -o CMakeFiles/mimalloc-static.dir/src/segment-map.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/segment-map.c

CMakeFiles/mimalloc-static.dir/src/segment-map.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/segment-map.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/segment-map.c > CMakeFiles/mimalloc-static.dir/src/segment-map.c.i

CMakeFiles/mimalloc-static.dir/src/segment-map.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/segment-map.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/segment-map.c -o CMakeFiles/mimalloc-static.dir/src/segment-map.c.s

CMakeFiles/mimalloc-static.dir/src/stats.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/stats.c.o: src/stats.c
CMakeFiles/mimalloc-static.dir/src/stats.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_14) "Building C object CMakeFiles/mimalloc-static.dir/src/stats.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/stats.c.o -MF CMakeFiles/mimalloc-static.dir/src/stats.c.o.d -o CMakeFiles/mimalloc-static.dir/src/stats.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/stats.c

CMakeFiles/mimalloc-static.dir/src/stats.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/stats.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/stats.c > CMakeFiles/mimalloc-static.dir/src/stats.c.i

CMakeFiles/mimalloc-static.dir/src/stats.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/stats.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/stats.c -o CMakeFiles/mimalloc-static.dir/src/stats.c.s

CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o: CMakeFiles/mimalloc-static.dir/flags.make
CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o: src/prim/prim.c
CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o: CMakeFiles/mimalloc-static.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_15) "Building C object CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o -MF CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o.d -o CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o -c /home/jakjm/Downloads/mimalloc-2.1.2/src/prim/prim.c

CMakeFiles/mimalloc-static.dir/src/prim/prim.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing C source to CMakeFiles/mimalloc-static.dir/src/prim/prim.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jakjm/Downloads/mimalloc-2.1.2/src/prim/prim.c > CMakeFiles/mimalloc-static.dir/src/prim/prim.c.i

CMakeFiles/mimalloc-static.dir/src/prim/prim.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling C source to assembly CMakeFiles/mimalloc-static.dir/src/prim/prim.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jakjm/Downloads/mimalloc-2.1.2/src/prim/prim.c -o CMakeFiles/mimalloc-static.dir/src/prim/prim.c.s

# Object files for target mimalloc-static
mimalloc__static_OBJECTS = \
"CMakeFiles/mimalloc-static.dir/src/alloc.c.o" \
"CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o" \
"CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o" \
"CMakeFiles/mimalloc-static.dir/src/arena.c.o" \
"CMakeFiles/mimalloc-static.dir/src/bitmap.c.o" \
"CMakeFiles/mimalloc-static.dir/src/heap.c.o" \
"CMakeFiles/mimalloc-static.dir/src/init.c.o" \
"CMakeFiles/mimalloc-static.dir/src/options.c.o" \
"CMakeFiles/mimalloc-static.dir/src/os.c.o" \
"CMakeFiles/mimalloc-static.dir/src/page.c.o" \
"CMakeFiles/mimalloc-static.dir/src/random.c.o" \
"CMakeFiles/mimalloc-static.dir/src/segment.c.o" \
"CMakeFiles/mimalloc-static.dir/src/segment-map.c.o" \
"CMakeFiles/mimalloc-static.dir/src/stats.c.o" \
"CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o"

# External object files for target mimalloc-static
mimalloc__static_EXTERNAL_OBJECTS =

libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/alloc.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/alloc-aligned.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/alloc-posix.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/arena.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/bitmap.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/heap.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/init.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/options.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/os.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/page.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/random.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/segment.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/segment-map.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/stats.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/src/prim/prim.c.o
libmimalloc.a: CMakeFiles/mimalloc-static.dir/build.make
libmimalloc.a: CMakeFiles/mimalloc-static.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles --progress-num=$(CMAKE_PROGRESS_16) "Linking C static library libmimalloc.a"
	$(CMAKE_COMMAND) -P CMakeFiles/mimalloc-static.dir/cmake_clean_target.cmake
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/mimalloc-static.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/mimalloc-static.dir/build: libmimalloc.a
.PHONY : CMakeFiles/mimalloc-static.dir/build

CMakeFiles/mimalloc-static.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/mimalloc-static.dir/cmake_clean.cmake
.PHONY : CMakeFiles/mimalloc-static.dir/clean

CMakeFiles/mimalloc-static.dir/depend:
	cd /home/jakjm/Downloads/mimalloc-2.1.2 && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jakjm/Downloads/mimalloc-2.1.2 /home/jakjm/Downloads/mimalloc-2.1.2 /home/jakjm/Downloads/mimalloc-2.1.2 /home/jakjm/Downloads/mimalloc-2.1.2 /home/jakjm/Downloads/mimalloc-2.1.2/CMakeFiles/mimalloc-static.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/mimalloc-static.dir/depend
