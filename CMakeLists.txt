# host-libretro: our libretro implementation, as a static library.
# Frontends (piccolo, libretro-test-frontend) link it in-process; an IPC
# process wrapper comes later for Multitap. No SDL, no GUI.
# A library: it doesn't build alone. The frontend adds hosts-common first and
# defines libretro_api (libretro.h); for the Test libretro tests, it adds
# test-libretro (target test_libretro) before this.

multitap_require(host-libretro host_common libretro_api)

add_library(host_libretro STATIC
    src/libretro_core.cpp
    src/libretro_host.cpp
    src/libretro_environment.cpp
    src/libretro_environment_names.cpp
    src/libretro_video.cpp
    src/libretro_audio.cpp
    src/libretro_options.cpp
    src/libretro_input.cpp
    src/libretro_subsystem.cpp
)

target_include_directories(host_libretro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# host-libretro's version (docs/roadmap-host-libretro.md): host_libretro_version.h,
# HOST_LIBRETRO_VERSION. The branch suffix is this repository's branch.
set(HOST_LIBRETRO_VERSION_BASE 0.3)
multitap_version(HOST_LIBRETRO_VERSION ${HOST_LIBRETRO_VERSION_BASE} "${CMAKE_CURRENT_SOURCE_DIR}")
multitap_write_version_header("${CMAKE_CURRENT_BINARY_DIR}/generated" host_libretro HOST_LIBRETRO_VERSION "${HOST_LIBRETRO_VERSION}")
target_include_directories(host_libretro PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
message(STATUS "host-libretro ${HOST_LIBRETRO_VERSION}")
target_link_libraries(host_libretro PUBLIC host_common libretro_api)

multitap_set_project_flags(host_libretro)

# A core linked into the program instead of loaded from a file
# (src/libretro_linked_core.h): the browser build. The frontend links this
# library and the core's static archive (which defines the retro_* functions).
add_library(host_libretro_linked STATIC src/libretro_linked_core.cpp)
target_link_libraries(host_libretro_linked PUBLIC host_libretro)
multitap_set_project_flags(host_libretro_linked)

# Development tool: load a core (+ content), log environment calls, print info, unload.
add_executable(libretro_core_info tools/core_info.cpp)
target_link_libraries(libretro_core_info PRIVATE host_libretro)
multitap_set_project_flags(libretro_core_info)

# ---------------------------------------------------------------------------
# Tests: our own minimal test core + libretro_core_info. Run with: ctest --test-dir build
# ---------------------------------------------------------------------------
if(MULTITAP_BUILD_TESTS)
    # MODULE = a plugin loaded with dlopen/LoadLibrary, like a real core.
    add_library(test_core MODULE tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE libretro_api)
    target_include_directories(test_core PRIVATE src)   # libretro_extensions.h, as a core would carry it
    set_target_properties(test_core PROPERTIES PREFIX "" OUTPUT_NAME "test_core_libretro")
    multitap_set_project_flags(test_core)

    # Same core without retro_run: the host must refuse it.
    add_library(test_core_missing_symbol MODULE tests/test_core.cpp)
    target_link_libraries(test_core_missing_symbol PRIVATE libretro_api)
    target_include_directories(test_core_missing_symbol PRIVATE src)
    target_compile_definitions(test_core_missing_symbol PRIVATE TEST_CORE_OMIT_RUN=1)
    set_target_properties(test_core_missing_symbol PROPERTIES PREFIX "" OUTPUT_NAME "test_core_missing_symbol_libretro")
    multitap_set_project_flags(test_core_missing_symbol)

    set(TEST_CONTENT_GOOD "${CMAKE_CURRENT_BINARY_DIR}/test_content_good.bin")
    set(TEST_CONTENT_BAD  "${CMAKE_CURRENT_BINARY_DIR}/test_content_bad.bin")
    file(WRITE "${TEST_CONTENT_GOOD}" "MULTITAP test content\n")
    file(WRITE "${TEST_CONTENT_BAD}"  "not the right marker\n")

    # Anything in this list appearing in the output fails a test, whatever the exit code.
    set(TEST_CORE_ERRORS "XRGB8888 refused|host accepted an invalid pixel format|host accepted GET_CAN_DUPE with NULL data|GET_INPUT_BITMASKS[^\n]*REJECTED|no current instance|ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    add_test(NAME host_libretro_no_game
             COMMAND libretro_core_info --system /tmp $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_no_game PROPERTIES FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # Multitap's run-ahead extension: the test core offers 3 frames; asked
    # for 5, it gets 3 (never more than it offered).
    add_test(NAME host_libretro_run_ahead
             COMMAND libretro_core_info --frames 3 --run-ahead 5 $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_run_ahead PROPERTIES
        PASS_REGULAR_EXPRESSION "run-ahead offered: the frontend drives it.*run-ahead frames: 3"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # Savestates in memory, hidden frames, quirks, savestate context.
    add_executable(host_libretro_state_memory_test tests/state_memory_test.cpp)
    target_link_libraries(host_libretro_state_memory_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_state_memory_test)
    add_test(NAME host_libretro_state_memory
             COMMAND host_libretro_state_memory_test $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_state_memory PROPERTIES
        PASS_REGULAR_EXPRESSION "state memory: all checks passed"
        FAIL_REGULAR_EXPRESSION "FAIL|${TEST_CORE_ERRORS}")
    # Multitap's text input extension: the test core asks for the keyboard,
    # then for a name; the answer (or the cancel) reaches its done callback.
    add_test(NAME host_libretro_text_input
             COMMAND libretro_core_info --frames 3 --text-answer ZELDA $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_text_input PROPERTIES
        PASS_REGULAR_EXPRESSION "text input KEYS: taken.*text input TEXT: taken.*text input answer: ZELDA"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")
    add_test(NAME host_libretro_text_input_cancel
             COMMAND libretro_core_info --frames 3 --text-cancel $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_text_input_cancel PROPERTIES
        PASS_REGULAR_EXPRESSION "text input TEXT: taken.*text input cancelled"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    add_test(NAME host_libretro_content_from_memory
             COMMAND libretro_core_info $<TARGET_FILE:test_core> "${TEST_CONTENT_GOOD}")
    set_tests_properties(host_libretro_content_from_memory PROPERTIES FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # The core rejects this content: the tool must exit non-zero and still unload cleanly.
    add_test(NAME host_libretro_content_rejected
             COMMAND libretro_core_info $<TARGET_FILE:test_core> "${TEST_CONTENT_BAD}")
    set_tests_properties(host_libretro_content_rejected PROPERTIES WILL_FAIL TRUE)

    add_test(NAME host_libretro_missing_content_file
             COMMAND libretro_core_info $<TARGET_FILE:test_core> "${CMAKE_CURRENT_BINARY_DIR}/does_not_exist.bin")
    set_tests_properties(host_libretro_missing_content_file PROPERTIES WILL_FAIL TRUE)

    add_test(NAME host_libretro_missing_symbol
             COMMAND libretro_core_info $<TARGET_FILE:test_core_missing_symbol>)
    set_tests_properties(host_libretro_missing_symbol PROPERTIES PASS_REGULAR_EXPRESSION "does not export retro_run")

    # A handled command sent with NULL data must be refused and logged, not crash.
    add_test(NAME host_libretro_null_data_refused
             COMMAND libretro_core_info $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_null_data_refused PROPERTIES PASS_REGULAR_EXPRESSION "GET_CAN_DUPE[^\n]*REJECTED[^\n]*NULL data")

    # GET_TARGET_SAMPLE_RATE: answered with the configured rate, not answered without one.
    add_test(NAME host_libretro_target_sample_rate
             COMMAND libretro_core_info --sample-rate 48000 $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_target_sample_rate PROPERTIES
                         PASS_REGULAR_EXPRESSION "target sample rate: 48000"
                         FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")
    add_test(NAME host_libretro_target_sample_rate_unset
             COMMAND libretro_core_info $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_target_sample_rate_unset PROPERTIES
                         PASS_REGULAR_EXPRESSION "target sample rate: not answered"
                         FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    add_test(NAME host_libretro_missing_core_file
             COMMAND libretro_core_info "${CMAKE_CURRENT_BINARY_DIR}/no_such_core.so")
    set_tests_properties(host_libretro_missing_core_file PROPERTIES WILL_FAIL TRUE)

    # Peeking: what a core says about itself without retro_init.
    add_test(NAME host_libretro_peek_test_core
             COMMAND libretro_core_info --peek $<TARGET_FILE:test_core> "some/dir.v2/GAME.BIN")
    set_tests_properties(host_libretro_peek_test_core PROPERTIES
        PASS_REGULAR_EXPRESSION "name \"multitap test core\".*extensions \"bin\\|txt\".*no game yes.*1 core options \\(API v0\\).*Peek: accepts some/dir\\.v2/GAME\\.BIN"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED|retro_init")

    add_test(NAME host_libretro_peek_wrong_extension
             COMMAND libretro_core_info --peek $<TARGET_FILE:test_core> "game.sfc")
    set_tests_properties(host_libretro_peek_wrong_extension PROPERTIES
        PASS_REGULAR_EXPRESSION "Peek: does NOT accept game\\.sfc")

    # The test core linked into the program: loaded, peeked and run by file name.
    add_executable(host_libretro_linked_core_test tests/linked_core_test.cpp tests/test_core.cpp)
    target_link_libraries(host_libretro_linked_core_test PRIVATE host_libretro_linked)
    target_include_directories(host_libretro_linked_core_test PRIVATE src)
    multitap_set_project_flags(host_libretro_linked_core_test)
    add_test(NAME host_libretro_linked_core COMMAND host_libretro_linked_core_test)
    set_tests_properties(host_libretro_linked_core PROPERTIES
        PASS_REGULAR_EXPRESSION "linked core: all checks passed"
        FAIL_REGULAR_EXPRESSION "FAIL|${TEST_CORE_ERRORS}")

    # A loaded core must not be peeked, whatever the path spelling (symlink, "//").
    add_executable(host_libretro_peek_loaded_test tests/peek_loaded_test.cpp)
    target_link_libraries(host_libretro_peek_loaded_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_peek_loaded_test)
    set(TEST_CORE_LINK "${CMAKE_CURRENT_BINARY_DIR}/test_core_link_libretro${CMAKE_SHARED_MODULE_SUFFIX}")
    add_custom_command(TARGET test_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:test_core> "${TEST_CORE_LINK}"
        VERBATIM)
    add_test(NAME host_libretro_peek_loaded
             COMMAND host_libretro_peek_loaded_test $<TARGET_FILE:test_core>
                     "$<TARGET_FILE_DIR:test_core>//$<TARGET_FILE_NAME:test_core>" "${TEST_CORE_LINK}")
    set_tests_properties(host_libretro_peek_loaded PROPERTIES
        PASS_REGULAR_EXPRESSION "peek loaded: all checks passed"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    add_test(NAME host_libretro_peek_missing_symbol
             COMMAND libretro_core_info --peek $<TARGET_FILE:test_core_missing_symbol>)
    set_tests_properties(host_libretro_peek_missing_symbol PROPERTIES
        PASS_REGULAR_EXPRESSION "Peek: not a usable libretro core")

    # Subsystems: the test core's "testlink" (cart A required, from memory;
    # cart B optional, by path), seen by peeking, and an entry without an
    # ident dropped.
    add_test(NAME host_libretro_peek_subsystems
             COMMAND libretro_core_info --peek $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_peek_subsystems PROPERTIES
        PASS_REGULAR_EXPRESSION "Subsystem \"No Ident\": no usable ident, dropped.*Subsystems: 1.*Subsystem testlink \"Test Link\" id 16: \"Test Cart A\" \\(bin, required, from memory, memory \\.sava 0x100\\) \\+ \"Test Cart B\" \\(bin\\|txt, optional, by path, memory \\.savb 0x101\\)"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|retro_init")

    add_test(NAME host_libretro_subsystem_two_files
             COMMAND libretro_core_info --frames 1 --subsystem testlink $<TARGET_FILE:test_core>
                     "${TEST_CONTENT_GOOD}" "${TEST_CONTENT_GOOD}")
    set_tests_properties(host_libretro_subsystem_two_files PROPERTIES
        PASS_REGULAR_EXPRESSION "Starting subsystem testlink.*special: testlink, cart A [0-9]+ bytes, cart B [^\n]*test_content_good\\.bin.*special: cart A save new"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    add_test(NAME host_libretro_subsystem_optional_left_out
             COMMAND libretro_core_info --frames 1 --subsystem testlink $<TARGET_FILE:test_core> "${TEST_CONTENT_GOOD}" none)
    set_tests_properties(host_libretro_subsystem_optional_left_out PROPERTIES
        PASS_REGULAR_EXPRESSION "\"Test Cart B\" left out.*special: testlink, cart A [0-9]+ bytes, cart B left out"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # Refused before the core is asked: a required file left out, an unknown
    # subsystem, the wrong number of files. And content the core rejects.
    add_test(NAME host_libretro_subsystem_required_missing
             COMMAND libretro_core_info --subsystem testlink $<TARGET_FILE:test_core> none "${TEST_CONTENT_GOOD}")
    set_tests_properties(host_libretro_subsystem_required_missing PROPERTIES
        PASS_REGULAR_EXPRESSION "\"Test Cart A\" is required"
        FAIL_REGULAR_EXPRESSION "Starting subsystem")

    add_test(NAME host_libretro_subsystem_unknown
             COMMAND libretro_core_info --subsystem sgb $<TARGET_FILE:test_core> "${TEST_CONTENT_GOOD}")
    set_tests_properties(host_libretro_subsystem_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "Subsystem sgb: the core has no such subsystem")

    add_test(NAME host_libretro_subsystem_file_count
             COMMAND libretro_core_info --subsystem testlink $<TARGET_FILE:test_core> "${TEST_CONTENT_GOOD}")
    set_tests_properties(host_libretro_subsystem_file_count PROPERTIES
        PASS_REGULAR_EXPRESSION "Subsystem testlink: 1 content files given, it takes 2")

    add_test(NAME host_libretro_subsystem_rejected
             COMMAND libretro_core_info --subsystem testlink $<TARGET_FILE:test_core> "${TEST_CONTENT_BAD}" none)
    set_tests_properties(host_libretro_subsystem_rejected PROPERTIES WILL_FAIL TRUE)

    # Each file's own save memory, named after that file: written on unload,
    # back on the next run (the core says "restored" on its first frame).
    set(SUBSYSTEM_DIR "${CMAKE_CURRENT_BINARY_DIR}/subsystem-test")
    add_test(NAME host_libretro_subsystem_clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${SUBSYSTEM_DIR}")
    set_tests_properties(host_libretro_subsystem_clean PROPERTIES FIXTURES_SETUP subsystem_clean)

    add_test(NAME host_libretro_subsystem_memory_first_run
             COMMAND libretro_core_info --data-dir "${SUBSYSTEM_DIR}" --frames 1 --subsystem testlink
                     $<TARGET_FILE:test_core> "${TEST_CONTENT_GOOD}" none)
    set_tests_properties(host_libretro_subsystem_memory_first_run PROPERTIES
        FIXTURES_REQUIRED subsystem_clean
        FIXTURES_SETUP subsystem_written
        PASS_REGULAR_EXPRESSION "Test Cart A \\.sava: 32 bytes, no [^\n]*test_core/test_content_good\\.sava yet.*cart A save new.*Test Cart A \\.sava: wrote 32 bytes"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|savb")

    add_test(NAME host_libretro_subsystem_memory_reload
             COMMAND libretro_core_info --data-dir "${SUBSYSTEM_DIR}" --frames 1 --subsystem testlink
                     $<TARGET_FILE:test_core> "${TEST_CONTENT_GOOD}" "${TEST_CONTENT_BAD}")
    set_tests_properties(host_libretro_subsystem_memory_reload PROPERTIES
        FIXTURES_REQUIRED subsystem_written
        PASS_REGULAR_EXPRESSION "Test Cart A \\.sava: loaded 32 bytes.*Test Cart B \\.savb: 8 bytes, no [^\n]*test_content_bad\\.savb yet.*cart A save restored"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # Pixel format conversion (all three libretro formats -> XRGB8888 + alpha).
    add_executable(host_libretro_video_test tests/video_conversion_test.cpp)
    target_link_libraries(host_libretro_video_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_video_test)
    add_test(NAME host_libretro_video_conversion COMMAND host_libretro_video_test)

    # Per-frame audio buffer (single-sample and batch appends, growth, clear).
    add_executable(host_libretro_audio_test tests/audio_buffer_test.cpp)
    target_link_libraries(host_libretro_audio_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_audio_test)
    add_test(NAME host_libretro_audio_buffer COMMAND host_libretro_audio_test)

    # Core options: v0/v1/v2 conversion, defaults, bad definitions, set/get/update, visibility.
    add_executable(host_libretro_options_test tests/options_test.cpp)
    target_link_libraries(host_libretro_options_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_options_test)
    add_test(NAME host_libretro_options COMMAND host_libretro_options_test)

    # Input: controller info and descriptors kept as data, port devices,
    # rumble, max users and device capabilities (with the test core).
    add_executable(host_libretro_input_test tests/input_test.cpp)
    target_link_libraries(host_libretro_input_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_input_test)
    add_test(NAME host_libretro_input COMMAND host_libretro_input_test $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_input PROPERTIES
        PASS_REGULAR_EXPRESSION "input: all checks passed"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # Our test core declares its option with SET_VARIABLES: keeps the oldest format covered end to end.
    add_test(NAME host_libretro_options_v0_test_core
             COMMAND libretro_core_info $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_options_v0_test_core PROPERTIES
        PASS_REGULAR_EXPRESSION "Option test_core_speed = normal \\(normal\\|fast\\)"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # The interactive Test libretro core (test_libretro, its own repository): when
    # the frontend added it. Its pages need a human, so CTest only runs headless
    # checks with it (below); the full walk-through is manual.
    if(TARGET test_libretro)

        # Headless smoke test: 120 frames of Test libretro (environment page) through the host.
        # Checks retro_run, video copy/conversion and the input path under the sanitizers.
        add_test(NAME host_libretro_test_libretro_runs
                 COMMAND libretro_core_info --frames 120 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_test_libretro_runs PROPERTIES
            PASS_REGULAR_EXPRESSION "Ran 120 frames: 120 video frames \\(640x480\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        # Controller types: Test libretro offers four per port; plugging the NES (257),
        # SNES (513) and PSX DualShock (261, an ANALOG subclass) style pads into ports
        # 1, 2 and 3 must bring each port's own descriptors, by the type's names.
        add_test(NAME host_libretro_test_libretro_controller_types
                 COMMAND libretro_core_info --device 1=257 --device 2=513 --device 3=261 --frames 10 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_test_libretro_controller_types PROPERTIES
            PASS_REGULAR_EXPRESSION "SET_CONTROLLER_INFO +\\( *35\\) handled +4 port\\(s\\), 16 device type\\(s\\).*Port 1 descriptors: 8 \\(D-Pad Up, D-Pad Down, D-Pad Left, D-Pad Right, B, A, Select, Start\\).*Port 2 descriptors: 12 \\(D-Pad Up, .*, Y, X, L, R, Select, Start\\).*Port 3 descriptors: 20 \\(D-Pad Up, .*, Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3, Select, Start, Left Stick, Left Stick, Right Stick, Right Stick\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        # Core options through each API Test libretro can pick (it follows the version
        # we report), then a width change at runtime: the core must re-read its
        # options, send SET_GEOMETRY and produce 720x480 frames.
        foreach(_version 2 1 0)
            add_test(NAME host_libretro_test_libretro_options_v${_version}
                     COMMAND libretro_core_info --options-version ${_version} --frames 10
                             --set libretro_test_width=720 $<TARGET_FILE:test_libretro>)
            set_tests_properties(host_libretro_test_libretro_options_v${_version} PROPERTIES
                PASS_REGULAR_EXPRESSION "Options: 5 declared with API v${_version}.*Option libretro_test_width = 640 \\(640\\|720\\).*SET_GEOMETRY +\\( *37\\) handled +720x480.*Ran 10 frames: 10 video frames \\(720x480\\)"
                FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")
        endforeach()

        # Saved core option values: applied when the core declares the option,
        # so the core reads them from its first GET_VARIABLE (720 wide from the
        # first frame, no SET_GEOMETRY). An invalid saved value is skipped
        # with a warning and the default applies.
        add_test(NAME host_libretro_test_libretro_saved_options
                 COMMAND libretro_core_info --frames 5 --saved libretro_test_width=720
                         --saved libretro_test_height=999 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_test_libretro_saved_options PROPERTIES
            PASS_REGULAR_EXPRESSION "Saved value \"999\" for core option libretro_test_height is not one of its values.*Option libretro_test_width = 720 .*Option libretro_test_height = 480 .*Ran 5 frames: 5 video frames \\(720x480\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED|SET_GEOMETRY")

        # Sample rate option at runtime: SET_SYSTEM_AV_INFO with the new rate, and
        # 32000 / 60 = 533.3 frames per run must add up to exactly one second of
        # audio in 60 frames (the core carries the fraction; one frame of slack
        # for rounding).
        add_test(NAME host_libretro_test_libretro_sample_rate
                 COMMAND libretro_core_info --frames 60 --set libretro_test_sample_rate=32000
                         $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_test_libretro_sample_rate PROPERTIES
            PASS_REGULAR_EXPRESSION "48000 Hz -> 640x480 @ 60 fps, 32000 Hz, SET_SYSTEM_AV_INFO accepted.*Ran 60 frames: 60 video frames \\(640x480\\), 0 duplicates, 0 video errors, 3(1999|2000) audio frames"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        # Save RAM: the host writes it on unload and loads it back on the next run.
        # Test libretro exposes a 60-byte block; untouched, the core reports it as "empty".
        set(SRAM_DIR "${CMAKE_CURRENT_BINARY_DIR}/sram-test")
        add_test(NAME host_libretro_sram_clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${SRAM_DIR}")
        set_tests_properties(host_libretro_sram_clean PROPERTIES FIXTURES_SETUP sram_clean)

        add_test(NAME host_libretro_sram_first_run
                 COMMAND libretro_core_info --data-dir "${SRAM_DIR}" --frames 2 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_sram_first_run PROPERTIES
            FIXTURES_REQUIRED sram_clean
            FIXTURES_SETUP sram_written
            PASS_REGULAR_EXPRESSION "SRAM: 60 bytes, no [^\n]*test\\.srm yet.*SRAM: wrote 60 bytes"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        add_test(NAME host_libretro_sram_reload
                 COMMAND libretro_core_info --data-dir "${SRAM_DIR}" --frames 2 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_sram_reload PROPERTIES
            FIXTURES_REQUIRED sram_written
            PASS_REGULAR_EXPRESSION "SRAM: loaded 60 bytes.*save RAM: empty"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED|CORRUPTED")

        # Junk in the .srm must be noticed by the core, not silently accepted.
        set(SRAM_BAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/sram-bad-test")
        file(WRITE "${SRAM_BAD_DIR}/saves/test/test.srm" "this is not the save RAM layout Test libretro writes!!!!!!!!")
        add_test(NAME host_libretro_sram_corrupted
                 COMMAND libretro_core_info --data-dir "${SRAM_BAD_DIR}" --frames 1 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_sram_corrupted PROPERTIES PASS_REGULAR_EXPRESSION "save RAM: CORRUPTED")

        # Savestates: save slot 3 after 50 frames, load it in a new run: the
        # core must be back at frame 50; an empty slot must be refused.
        set(STATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/state-test")
        add_test(NAME host_libretro_state_clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${STATE_DIR}")
        set_tests_properties(host_libretro_state_clean PROPERTIES FIXTURES_SETUP state_clean)

        add_test(NAME host_libretro_state_save
                 COMMAND libretro_core_info --data-dir "${STATE_DIR}" --frames 50 --save-state 3 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_state_save PROPERTIES
            FIXTURES_REQUIRED state_clean
            FIXTURES_SETUP state_saved
            PASS_REGULAR_EXPRESSION "Savestate: saved slot 3 \\([0-9]+ bytes\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        add_test(NAME host_libretro_state_load
                 COMMAND libretro_core_info --data-dir "${STATE_DIR}" --load-state 3 --frames 5 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_state_load PROPERTIES
            FIXTURES_REQUIRED state_saved
            PASS_REGULAR_EXPRESSION "state loaded: frame 50, page Callbacks.*Savestate: loaded slot 3"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        # Slots are 1..9: slot 0 is refused.
        add_test(NAME host_libretro_state_no_slot_0
                 COMMAND libretro_core_info --data-dir "${STATE_DIR}" --frames 2 --save-state 0 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_state_no_slot_0 PROPERTIES
            FIXTURES_REQUIRED state_clean
            PASS_REGULAR_EXPRESSION "Savestate: no slot 0")

        add_test(NAME host_libretro_state_empty_slot
                 COMMAND libretro_core_info --data-dir "${STATE_DIR}" --load-state 7 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_state_empty_slot PROPERTIES
            FIXTURES_REQUIRED state_saved
            PASS_REGULAR_EXPRESSION "Savestate: slot 7 is empty")

        # Height and frame rate change the core's maximum size and timing: they must
        # come through SET_SYSTEM_AV_INFO, and the frames that follow must match.
        add_test(NAME host_libretro_test_libretro_av_info
                 COMMAND libretro_core_info --frames 10 --set libretro_test_height=720 --set libretro_test_fps=30
                         $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_test_libretro_av_info PROPERTIES
            PASS_REGULAR_EXPRESSION "SET_SYSTEM_AV_INFO +\\( *32\\) handled +640x720 @ 30\\.000 fps.*Ran 10 frames: 10 video frames \\(640x720\\), 0 duplicates, 0 video errors, 16000 audio frames"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        add_test(NAME host_libretro_peek_test_libretro
                 COMMAND libretro_core_info --peek $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_peek_test_libretro PROPERTIES
            PASS_REGULAR_EXPRESSION "name \"Test libretro\".*no game yes.*5 core options \\(API v2\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED|\\] init")

        # A value that isn't one of the option's values must be refused.
        add_test(NAME host_libretro_option_bad_value
                 COMMAND libretro_core_info --set libretro_test_width=800 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_option_bad_value PROPERTIES
            PASS_REGULAR_EXPRESSION "libretro_test_width: '800' is not one of its values")
    endif()
endif()
