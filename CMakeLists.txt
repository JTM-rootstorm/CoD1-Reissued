cmake_minimum_required(VERSION 3.25)
project(cod1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# x86recomp: the lifter, x87 model and runtime (a sibling checkout).
set(X86RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../x86recomp"
    CACHE PATH "x86recomp source tree")
# Graphics go through x86recomp's SDL_GPU backend (Vulkan, Metal on macOS).
# dxvk-native (d3d9 only, SDL3 WSI) is opt-in and not built by default:
# configure with -DDXVK_NATIVE_DIR=<its install prefix> (as the Dungeon
# Siege project builds it, ../../Dungeon Siege/tools_ext/dxvk-native) to add the d3d9
# reference backend (COD1_GFX=d3d9).
set(DXVK_NATIVE_DIR "" CACHE PATH "dxvk-native install prefix (opt-in d3d9 backend)")

enable_testing()
add_subdirectory("${X86RECOMP_DIR}" x86recomp)

# The game: runtime + HLE + lifted code (generated/, produced by
# recomp_lift; see CLAUDE.md for the command).
recomp_add_lifted(cod1_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(TARGET cod1_lifted)
  add_executable(cod1 src/main.cpp src/cinematic.cpp src/skinning.cpp src/cm_trace.cpp
                 src/fov.cpp src/video_mode.cpp src/frame_pacing.cpp
                 src/screen2d.cpp
                 src/pmove_fixed.cpp src/footsteps.cpp
                 $<TARGET_OBJECTS:cod1_lifted>)
  target_link_libraries(cod1 PRIVATE recomp_runtime recomp_mss32 recomp_d3d7
                        recomp_opengl)
  set_target_properties(cod1 PROPERTIES ENABLE_EXPORTS ON)  # symbol names in fault backtraces
  if(DXVK_NATIVE_DIR)
    set_target_properties(cod1 PROPERTIES BUILD_RPATH "${DXVK_NATIVE_DIR}/lib64")
  endif()
  # Dev only: records calls of the CPU skinning functions for
  # skinning_oracle (tests/skinning_capture.cpp).
  option(COD1_SKINNING_CAPTURE "record CPU skinning calls (COD1_SKINNING_CAPTURE=file)" OFF)
  if(COD1_SKINNING_CAPTURE)
    target_sources(cod1 PRIVATE tests/skinning_capture.cpp)
    target_link_options(cod1 PRIVATE
      "LINKER:--wrap=_ZN6recomp3gen10f_005070E0ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_00507D20ERNS_3CtxE")
  endif()
  # Dev only: records collision traces and the clip map for cm_trace_oracle
  # (tests/cm_trace_capture.cpp).
  option(COD1_CM_CAPTURE "record collision traces (COD1_CM_CAPTURE=file)" OFF)
  if(COD1_CM_CAPTURE)
    target_sources(cod1 PRIVATE tests/cm_trace_capture.cpp)
    target_link_options(cod1 PRIVATE
      "LINKER:--wrap=_ZN6recomp3gen10f_0041CD60ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041D380ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041D3B0ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041D870ERNS_3CtxE")
  endif()
endif()
# The CPU skinning is bit-exact only without contraction into FMA, and is
# per-vertex work: optimised in every build type. It reads guest memory
# like lifted code, so a fault in it may unwind the same way
# (-fnon-call-exceptions and, on macOS, frame records in leaf functions:
# see recomp_add_lifted).
set(_skinning_options -O2 -ffp-contract=off -fnon-call-exceptions
    ${RECOMP_FAULT_FRAME_FLAGS})
set_source_files_properties(src/skinning.cpp PROPERTIES COMPILE_OPTIONS
  "${_skinning_options}")
# The collision traces likewise (x87 results in host doubles, src/x87_value.h),
# reading the clip map through overlays of guest memory.
set_source_files_properties(src/cm_trace.cpp PROPERTIES COMPILE_OPTIONS
  "${_skinning_options};-fno-strict-aliasing")
# The footstep rate in the original's order of operations, unfused.
set_source_files_properties(src/footsteps.cpp PROPERTIES COMPILE_OPTIONS
  -ffp-contract=off)

# Oracle tests over every instruction form in the EXE. Configure with
#   -DCOD1_GAME_EXE=unpacked/CoDSP.exe
set(COD1_GAME_EXE "" CACHE FILEPATH "unpacked CoDSP.exe for EXE-derived tests")
recomp_add_lift_tests(EXE "${COD1_GAME_EXE}"
  SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
  MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest")

# The native CPU skinning (src/skinning.cpp) against the original
# functions, lifted from COD1_GAME_EXE at build time with --only (into the
# build tree) and run on the SSE model: tests/skinning_test.cpp. Captures
# of real calls (tests/skinning_capture.cpp) can be passed as arguments.
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/skinning_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  add_custom_command(OUTPUT ${_gen}
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            --only 005070E0,00507D20,0051CA3C,0051D2B0,0051D2C1,0052CDBF --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
    COMMENT "Lifting the CPU skinning functions for skinning_oracle_test")
  add_executable(skinning_oracle_test tests/skinning_test.cpp src/skinning.cpp ${_gen})
  target_include_directories(skinning_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(skinning_oracle_test PRIVATE recomp_x87 recomp_image)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  add_test(NAME skinning_oracle COMMAND skinning_oracle_test)
endif()

# The native collision traces (src/cm_trace.cpp) against the original
# functions, lifted from COD1_GAME_EXE like skinning_oracle's (listed too:
# the leaf callback 0x41A4D0, called through a pointer, and the cases of
# 0x429800, which fills its own jump table):
# tests/cm_trace_test.cpp, on random clip maps, and on captured calls
# (tests/cm_trace_capture.cpp) listed in COD1_CM_CAPTURES.
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/cm_trace_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  add_custom_command(OUTPUT ${_gen}
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            --only 0041CD60,0041D380,0041D3B0,0041D870,0041A4D0,00429888,004298BB,004298EE,00429921,00429954,00429987,004299B7,004299E7,0051CA3C,0051D2B0,0051D2C1,0052A7C2,0052CDBF --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
    COMMENT "Lifting the collision traces for cm_trace_oracle_test")
  add_executable(cm_trace_oracle_test tests/cm_trace_test.cpp tests/cm_trace_native.cpp ${_gen})
  target_include_directories(cm_trace_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(cm_trace_oracle_test PRIVATE recomp_x87 recomp_image)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  set_source_files_properties(tests/cm_trace_native.cpp PROPERTIES COMPILE_OPTIONS
    "${_skinning_options};-fno-strict-aliasing")
  set(COD1_CM_CAPTURES "" CACHE STRING "captured traces for cm_trace_oracle (a list of files)")
  add_test(NAME cm_trace_oracle COMMAND cm_trace_oracle_test "${COD1_GAME_EXE}" ${COD1_CM_CAPTURES})
endif()

# x86recomp's Miles-compatible digital reverb, and its Miles 6.5 sample
# levels and Fast 2D, against the original routines of the installed
# game's mss32.dll (Miles 6.5e) and miles/msssoft.m3d next to it, lifted
# into the build tree at build time. Configure with
#   -DCOD1_MSS32_DLL="<Call of Duty>/mss32.dll"
set(COD1_MSS32_DLL "" CACHE FILEPATH
    "the game's mss32.dll for digital_reverb_oracle and fast2d_oracle")
recomp_add_digital_reverb_test(DLL "${COD1_MSS32_DLL}")
if(COD1_MSS32_DLL)
  get_filename_component(_miles_dir "${COD1_MSS32_DLL}" DIRECTORY)
  recomp_add_fast2d_test(MSS32 "${COD1_MSS32_DLL}" FAST2D "${_miles_dir}/miles/msssoft.m3d")
endif()
