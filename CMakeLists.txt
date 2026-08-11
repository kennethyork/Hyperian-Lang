cmake_minimum_required(VERSION 3.16)
project(Hyperian VERSION 0.34.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_executable(hyperian
    src/main.c
    src/compiler.c
    src/bytecode.c
    src/runtime.c
    src/mobile.c
    src/desktop.c
    src/game.c
    src/network.c
    src/security.c
)
target_include_directories(hyperian PRIVATE src)
target_compile_options(hyperian PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(hyperian PRIVATE HYPERIAN_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    pkg_check_modules(CURL QUIET IMPORTED_TARGET libcurl)
    pkg_check_modules(SQLITE3 QUIET IMPORTED_TARGET sqlite3)
endif()
if(SQLITE3_FOUND)
    target_compile_definitions(hyperian PRIVATE HYPERIAN_HAVE_SQLITE3=1)
    target_link_libraries(hyperian PRIVATE PkgConfig::SQLITE3)
endif()
if(CURL_FOUND)
    target_compile_definitions(hyperian PRIVATE HYPERIAN_HAVE_CURL=1)
    target_link_libraries(hyperian PRIVATE PkgConfig::CURL)
endif()
if(GTK3_FOUND)
    target_compile_definitions(hyperian PRIVATE HYPERIAN_HAVE_GTK3=1)
    target_link_libraries(hyperian PRIVATE PkgConfig::GTK3)
endif()
if(SDL2_FOUND)
    target_compile_definitions(hyperian PRIVATE HYPERIAN_HAVE_SDL2=1)
    target_link_libraries(hyperian PRIVATE PkgConfig::SDL2)
endif()

install(TARGETS hyperian RUNTIME DESTINATION bin)

add_library(hyperian_mobile_runtime STATIC
    src/mobile.c src/runtime.c src/bytecode.c src/network.c src/security.c src/desktop.c src/game.c)
set_target_properties(hyperian_mobile_runtime PROPERTIES OUTPUT_NAME hyperian_mobile POSITION_INDEPENDENT_CODE ON)
target_include_directories(hyperian_mobile_runtime PUBLIC src)
target_compile_options(hyperian_mobile_runtime PRIVATE -Wall -Wextra -Wpedantic)
if(SQLITE3_FOUND)
    target_compile_definitions(hyperian_mobile_runtime PRIVATE HYPERIAN_HAVE_SQLITE3=1)
    target_link_libraries(hyperian_mobile_runtime PRIVATE PkgConfig::SQLITE3)
endif()
if(CURL_FOUND)
    target_compile_definitions(hyperian_mobile_runtime PRIVATE HYPERIAN_HAVE_CURL=1)
    target_link_libraries(hyperian_mobile_runtime PRIVATE PkgConfig::CURL)
endif()
if(GTK3_FOUND)
    target_compile_definitions(hyperian_mobile_runtime PRIVATE HYPERIAN_HAVE_GTK3=1)
    target_link_libraries(hyperian_mobile_runtime PRIVATE PkgConfig::GTK3)
endif()
if(SDL2_FOUND)
    target_compile_definitions(hyperian_mobile_runtime PRIVATE HYPERIAN_HAVE_SDL2=1)
    target_link_libraries(hyperian_mobile_runtime PRIVATE PkgConfig::SDL2)
endif()
install(TARGETS hyperian_mobile_runtime ARCHIVE DESTINATION lib)
install(FILES src/hyperian.h DESTINATION include/hyperian)
install(DIRECTORY platform/android DESTINATION share/hyperian/platform)
install(DIRECTORY platform/ios DESTINATION share/hyperian/platform)
install(FILES src/mobile.c src/runtime.c src/bytecode.c src/network.c src/security.c src/security.h src/desktop.c src/game.c src/hyperian.h
    DESTINATION share/hyperian/runtime)

enable_testing()
add_test(NAME compiler_help COMMAND hyperian help)
add_test(NAME toolchain_doctor COMMAND hyperian doctor)
set_tests_properties(toolchain_doctor PROPERTIES PASS_REGULAR_EXPRESSION "compiler and bytecode VM: ready")
add_test(NAME compile_example
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/tasks.hyp -o ${CMAKE_BINARY_DIR}/tasks.hyc)
add_test(NAME inspect_example
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/tasks.hyc)
set_tests_properties(inspect_example PROPERTIES DEPENDS compile_example)
add_test(NAME compile_pwa
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/pwa_tasks/app.hyp -o ${CMAKE_BINARY_DIR}/pwa_tasks.hyc)
add_test(NAME reject_pwa_without_route
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_pwa_route.hyp)
set_tests_properties(reject_pwa_without_route PROPERTIES WILL_FAIL TRUE)
find_program(CURL_EXECUTABLE curl)
if(CURL_EXECUTABLE)
    add_test(NAME run_pwa_runtime
        COMMAND sh ${CMAKE_SOURCE_DIR}/tests/pwa_runtime_test.sh $<TARGET_FILE:hyperian> ${CMAKE_SOURCE_DIR}/examples/pwa_tasks/app.hyp ${CMAKE_BINARY_DIR})
endif()
add_test(NAME clean_pwa_bundle COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/PwaBundle)
add_test(NAME build_pwa_bundle
    COMMAND hyperian bundle ${CMAKE_SOURCE_DIR}/examples/pwa_tasks/app.hyp -o ${CMAKE_BINARY_DIR}/PwaBundle)
set_tests_properties(build_pwa_bundle PROPERTIES DEPENDS clean_pwa_bundle)
add_test(NAME verify_pwa_bundle_worker
    COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_SOURCE_DIR}/examples/pwa_tasks/public/service-worker.js ${CMAKE_BINARY_DIR}/PwaBundle/public/service-worker.js)
set_tests_properties(verify_pwa_bundle_worker PROPERTIES DEPENDS build_pwa_bundle)
add_test(NAME debug_native_game
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/blocks_game.hyp)
set_tests_properties(debug_native_game PROPERTIES PASS_REGULAR_EXPRESSION "player_x = 100")
add_test(NAME reject_missing_view
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_missing_view.hyp)
set_tests_properties(reject_missing_view PROPERTIES WILL_FAIL TRUE)
add_test(NAME compile_console
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/greeter.hyp -o ${CMAKE_BINARY_DIR}/greeter.hyc)
add_test(NAME run_console
    COMMAND sh -c "printf 'Ada\\n' | '$<TARGET_FILE:hyperian>' run '${CMAKE_BINARY_DIR}/greeter.hyc' | grep 'Ada'")
set_tests_properties(run_console PROPERTIES DEPENDS compile_console)
add_test(NAME compile_crud
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/notes_crud.hyp -o ${CMAKE_BINARY_DIR}/notes_crud.hyc)
add_test(NAME clean_native_crud_data COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/native_crud.hdb)
add_test(NAME compile_native_crud
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/native_crud.hyp -o ${CMAKE_BINARY_DIR}/native_crud.hyc)
add_test(NAME run_native_crud
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/native_crud.hdb HYPERIAN_CONSOLE_TEST_STATE=remaining
        $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/native_crud.hyc)
set_tests_properties(run_native_crud PROPERTIES DEPENDS "clean_native_crud_data;compile_native_crud" PASS_REGULAR_EXPRESSION "remaining=0")
add_test(NAME clean_native_queries_data COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/native_queries.hdb)
add_test(NAME compile_native_queries
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/native_queries.hyp -o ${CMAKE_BINARY_DIR}/native_queries.hyc)
add_test(NAME run_native_queries
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/native_queries.hdb HYPERIAN_CONSOLE_TEST_STATE=query_result
        $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/native_queries.hyc)
set_tests_properties(run_native_queries PROPERTIES DEPENDS "clean_native_queries_data;compile_native_queries" PASS_REGULAR_EXPRESSION "query_result=Alpha/Gamma/Gamma/2/Alpha")
add_test(NAME reject_duplicate_field
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_duplicate_field.hyp)
set_tests_properties(reject_duplicate_field PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_crud_without_id
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_crud_route.hyp)
set_tests_properties(reject_crud_without_id PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_secret_collection
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_collect_secret.hyp)
set_tests_properties(reject_secret_collection PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_secret_query_filter COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_query_secret_filter.hyp)
set_tests_properties(reject_secret_query_filter PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_secret_query_order COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_query_secret_order.hyp)
set_tests_properties(reject_secret_query_order PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_missing_query_field COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_query_missing_field.hyp)
set_tests_properties(reject_missing_query_field PROPERTIES WILL_FAIL TRUE)
add_test(NAME compile_api_relationships
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/blog_api.hyp -o ${CMAKE_BINARY_DIR}/blog_api.hyc)
add_test(NAME compile_authentication
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/accounts.hyp -o ${CMAKE_BINARY_DIR}/accounts.hyc)

add_executable(hyperian_security_test tests/security_test.c src/security.c)
target_include_directories(hyperian_security_test PRIVATE src)
target_compile_options(hyperian_security_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME native_security COMMAND hyperian_security_test)
add_test(NAME compile_english_logic
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/english_logic.hyp -o ${CMAKE_BINARY_DIR}/english_logic.hyc)
add_test(NAME run_service
    COMMAND hyperian run ${CMAKE_SOURCE_DIR}/examples/report_service.hyp)
add_test(NAME compile_scheduled_service
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/scheduled_service.hyp -o ${CMAKE_BINARY_DIR}/scheduled_service.hyc)
add_test(NAME run_scheduled_service
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_SERVICE_TEST_TICKS=3 HYPERIAN_SERVICE_TEST_STATE=runs
        $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/scheduled_service.hyc)
set_tests_properties(run_scheduled_service PROPERTIES DEPENDS compile_scheduled_service PASS_REGULAR_EXPRESSION "runs=6")
add_test(NAME reject_bad_timer COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_timer.hyp)
set_tests_properties(reject_bad_timer PROPERTIES WILL_FAIL TRUE)
add_test(NAME build_standalone_service
    COMMAND hyperian build ${CMAKE_SOURCE_DIR}/examples/report_service.hyp -o ${CMAKE_BINARY_DIR}/standalone_report)
add_test(NAME run_standalone_service
    COMMAND ${CMAKE_BINARY_DIR}/standalone_report)
set_tests_properties(run_standalone_service PROPERTIES DEPENDS build_standalone_service)
add_test(NAME clean_console_bundle COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/BundleConsole)
add_test(NAME build_console_bundle
    COMMAND hyperian bundle ${CMAKE_SOURCE_DIR}/examples/bundle_console/app.hyp -o ${CMAKE_BINARY_DIR}/BundleConsole)
set_tests_properties(build_console_bundle PROPERTIES DEPENDS clean_console_bundle)
add_test(NAME run_console_bundle COMMAND ${CMAKE_BINARY_DIR}/BundleConsole/run)
set_tests_properties(run_console_bundle PROPERTIES DEPENDS build_console_bundle WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} PASS_REGULAR_EXPRESSION "Hello from a bundled asset")
add_test(NAME format_source
    COMMAND hyperian format ${CMAKE_SOURCE_DIR}/examples/english_logic.hyp)
add_test(NAME english_language_tests
    COMMAND hyperian test ${CMAKE_SOURCE_DIR}/examples/english_logic.hyp)
add_test(NAME compile_general_purpose_core
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/core_features/app.hyp -o ${CMAKE_BINARY_DIR}/core_features.hyc)
if(CURL_FOUND)
    add_test(NAME general_purpose_english_tests
        COMMAND hyperian test ${CMAKE_SOURCE_DIR}/examples/core_features/app.hyp)
    add_test(NAME compile_http_client
        COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/http_client.hyp -o ${CMAKE_BINARY_DIR}/http_client.hyc)
endif()
add_test(NAME reject_try_without_handler
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_try_without_handler.hyp)
set_tests_properties(reject_try_without_handler PROPERTIES WILL_FAIL TRUE)
add_test(NAME compile_multi_file_program
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/modular/app.hyp -o ${CMAKE_BINARY_DIR}/modular.hyc)
add_test(NAME compile_foldered_mvc_program
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/foldered_app/app.hyp -o ${CMAKE_BINARY_DIR}/foldered_app.hyc)
add_test(NAME concurrent_compiler_tooling
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/concurrent_tooling_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp)
add_test(NAME compile_native_desktop
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp -o ${CMAKE_BINARY_DIR}/desktop_notes.hyc)
find_program(XVFB_RUN xvfb-run)
if(GTK3_FOUND AND XVFB_RUN)
    add_test(NAME clean_desktop_input_event_data COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/desktop_input_events.hdb)
    add_test(NAME run_desktop_input_event
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/desktop_input_events.hdb
            HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_INPUT=Hello HYPERIAN_VISUAL_TEST_STATE=live_preview
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp)
    set_tests_properties(run_desktop_input_event PROPERTIES DEPENDS "compile_native_desktop;clean_desktop_input_event_data" PASS_REGULAR_EXPRESSION "live_preview=Typing:Hello")
    add_test(NAME clean_desktop_submit_event_data COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/desktop_submit_events.hdb)
    add_test(NAME run_desktop_submit_event
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/desktop_submit_events.hdb
            HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_INPUT=Hello HYPERIAN_VISUAL_TEST_SUBMIT=title HYPERIAN_VISUAL_TEST_STATE=status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp)
    set_tests_properties(run_desktop_submit_event PROPERTIES DEPENDS "compile_native_desktop;clean_desktop_submit_event_data" PASS_REGULAR_EXPRESSION "status=Saved note:")
    add_test(NAME run_desktop_textarea_change_event
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/desktop_textarea_events.hdb HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_INPUT_NAME=details
            HYPERIAN_VISUAL_TEST_INPUT=Hello HYPERIAN_VISUAL_TEST_STATE=status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp)
    set_tests_properties(run_desktop_textarea_change_event PROPERTIES DEPENDS compile_native_desktop PASS_REGULAR_EXPRESSION "status=Editing details")
    add_test(NAME run_desktop_checkbox_change_event
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/desktop_checkbox_events.hdb HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_INPUT_NAME=important
            HYPERIAN_VISUAL_TEST_INPUT=true HYPERIAN_VISUAL_TEST_STATE=status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp)
    set_tests_properties(run_desktop_checkbox_change_event PROPERTIES DEPENDS compile_native_desktop PASS_REGULAR_EXPRESSION "status=Importance changed")
    add_test(NAME run_desktop_focus_event
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_EVENT=FOCUS HYPERIAN_VISUAL_TEST_STATE=window_status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp)
    set_tests_properties(run_desktop_focus_event PROPERTIES DEPENDS compile_native_desktop PASS_REGULAR_EXPRESSION "window_status=Window focused")
    add_test(NAME run_desktop_blur_event
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_EVENT=BLUR HYPERIAN_VISUAL_TEST_STATE=window_status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp)
    set_tests_properties(run_desktop_blur_event PROPERTIES DEPENDS compile_native_desktop PASS_REGULAR_EXPRESSION "window_status=Window unfocused")
    add_test(NAME run_mobile_preview_swipe
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_SWIPE=left HYPERIAN_VISUAL_TEST_STATE=gesture_status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp)
    set_tests_properties(run_mobile_preview_swipe PROPERTIES DEPENDS compile_mobile_preview PASS_REGULAR_EXPRESSION "gesture_status=Swiped left")
    add_test(NAME run_mobile_preview_tap
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_GESTURE=tap HYPERIAN_VISUAL_TEST_STATE=gesture_status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp)
    set_tests_properties(run_mobile_preview_tap PROPERTIES DEPENDS compile_mobile_preview PASS_REGULAR_EXPRESSION "gesture_status=Tapped")
    add_test(NAME run_mobile_preview_hold
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_GESTURE=hold HYPERIAN_VISUAL_TEST_STATE=gesture_status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp)
    set_tests_properties(run_mobile_preview_hold PROPERTIES DEPENDS compile_mobile_preview PASS_REGULAR_EXPRESSION "gesture_status=Pressed and held")
endif()
add_test(NAME compile_mobile_preview
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp -o ${CMAKE_BINARY_DIR}/mobile_tasks.hyc)
add_executable(hyperian_mobile_bridge_test tests/mobile_bridge_test.c)
target_include_directories(hyperian_mobile_bridge_test PRIVATE src)
target_compile_options(hyperian_mobile_bridge_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(hyperian_mobile_bridge_test PRIVATE hyperian_mobile_runtime)
add_test(NAME clean_mobile_bridge_data COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/mobile_bridge.hdb)
add_test(NAME run_mobile_bridge
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/mobile_bridge.hdb
        $<TARGET_FILE:hyperian_mobile_bridge_test> ${CMAKE_BINARY_DIR}/mobile_tasks.hyc)
set_tests_properties(run_mobile_bridge PROPERTIES DEPENDS "compile_mobile_preview;clean_mobile_bridge_data")
add_test(NAME clean_android_export COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/AndroidExport)
add_test(NAME build_android_export
    COMMAND hyperian export ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp for android to ${CMAKE_BINARY_DIR}/AndroidExport)
set_tests_properties(build_android_export PROPERTIES DEPENDS clean_android_export)
add_test(NAME verify_android_export
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/mobile_export_test.sh ${CMAKE_BINARY_DIR}/AndroidExport android)
set_tests_properties(verify_android_export PROPERTIES DEPENDS build_android_export)
add_test(NAME clean_ios_export COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/IosExport)
add_test(NAME build_ios_export
    COMMAND hyperian export ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp for ios to ${CMAKE_BINARY_DIR}/IosExport)
set_tests_properties(build_ios_export PROPERTIES DEPENDS clean_ios_export)
add_test(NAME verify_ios_export
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/mobile_export_test.sh ${CMAKE_BINARY_DIR}/IosExport ios)
set_tests_properties(verify_ios_export PROPERTIES DEPENDS build_ios_export)
add_test(NAME reject_web_mobile_export
    COMMAND hyperian export ${CMAKE_SOURCE_DIR}/examples/tasks.hyp for android to ${CMAKE_BINARY_DIR}/BadMobileExport)
set_tests_properties(reject_web_mobile_export PROPERTIES WILL_FAIL TRUE)
add_test(NAME debug_mobile_navigation
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp --action "show completed")
set_tests_properties(debug_mobile_navigation PROPERTIES PASS_REGULAR_EXPRESSION "__hyperian_open_view = completed")
add_test(NAME debug_english_input_event
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp --event "input title changes")
set_tests_properties(debug_english_input_event PROPERTIES PASS_REGULAR_EXPRESSION "Debugging event input title changes")
add_test(NAME debug_english_lifecycle_event
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp --event "application resumes")
set_tests_properties(debug_english_lifecycle_event PROPERTIES PASS_REGULAR_EXPRESSION "lifecycle_status is now Application resumed")
add_test(NAME debug_english_swipe_event
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp --event "someone swipes right")
set_tests_properties(debug_english_swipe_event PROPERTIES PASS_REGULAR_EXPRESSION "gesture_status is now Swiped right")
add_test(NAME debug_english_tap_event
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp --event "someone taps")
set_tests_properties(debug_english_tap_event PROPERTIES PASS_REGULAR_EXPRESSION "gesture_status is now Tapped")
add_test(NAME debug_english_hold_event
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp --event "someone presses and holds")
set_tests_properties(debug_english_hold_event PROPERTIES PASS_REGULAR_EXPRESSION "gesture_status is now Pressed and held")
add_test(NAME reject_missing_desktop_action
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_desktop_action.hyp)
set_tests_properties(reject_missing_desktop_action PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_missing_input_event
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_missing_input_event.hyp)
set_tests_properties(reject_missing_input_event PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_textarea_submission
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_textarea_submission.hyp)
set_tests_properties(reject_textarea_submission PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_input_event_outside_native_interface
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_input_event_target.hyp)
set_tests_properties(reject_input_event_outside_native_interface PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_mobile_lifecycle_outside_mobile
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_mobile_lifecycle_target.hyp)
set_tests_properties(reject_mobile_lifecycle_outside_mobile PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_focus_event_outside_native_interface
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_focus_event_target.hyp)
set_tests_properties(reject_focus_event_outside_native_interface PROPERTIES WILL_FAIL TRUE)
add_test(NAME format_native_events
    COMMAND hyperian format ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp)
set_tests_properties(format_native_events PROPERTIES PASS_REGULAR_EXPRESSION "    when application pauses")
add_test(NAME reject_swipe_outside_mobile
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_swipe_target.hyp)
set_tests_properties(reject_swipe_outside_mobile PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_bad_swipe_direction
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_swipe_direction.hyp)
set_tests_properties(reject_bad_swipe_direction PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_tap_outside_mobile
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_tap_target.hyp)
set_tests_properties(reject_tap_outside_mobile PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_hold_outside_mobile
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_hold_target.hyp)
set_tests_properties(reject_hold_outside_mobile PROPERTIES WILL_FAIL TRUE)
add_test(NAME compile_native_game
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/blocks_game.hyp -o ${CMAKE_BINARY_DIR}/blocks_game.hyc)
add_test(NAME compile_game_physics
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/physics_game.hyp -o ${CMAKE_BINARY_DIR}/physics_game.hyc)
add_executable(hyperian_game_physics_test tests/game_physics_test.c)
target_include_directories(hyperian_game_physics_test PRIVATE src)
target_compile_options(hyperian_game_physics_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(hyperian_game_physics_test PRIVATE hyperian_mobile_runtime)
add_test(NAME run_game_physics
    COMMAND hyperian_game_physics_test ${CMAKE_BINARY_DIR}/physics_game.hyc)
set_tests_properties(run_game_physics PROPERTIES DEPENDS compile_game_physics)
add_test(NAME reject_physics_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_physics_target.hyp)
set_tests_properties(reject_physics_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_animation_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_animation_target.hyp)
set_tests_properties(reject_animation_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_bad_animation_interval
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_animation_interval.hyp)
set_tests_properties(reject_bad_animation_interval PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_bad_animation_frames_at_runtime
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/tests/bad_animation_frames.hyp --event FRAME)
set_tests_properties(reject_bad_animation_frames_at_runtime PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_bad_animation_speed_at_runtime
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/tests/bad_animation_speed.hyp --event FRAME)
set_tests_properties(reject_bad_animation_speed_at_runtime PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_bad_animation_number_at_runtime
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/tests/bad_animation_number.hyp --event FRAME)
set_tests_properties(reject_bad_animation_number_at_runtime PROPERTIES WILL_FAIL TRUE)
add_test(NAME inspect_game_animation
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/physics_game.hyc)
set_tests_properties(inspect_game_animation PROPERTIES DEPENDS compile_game_physics PASS_REGULAR_EXPRESSION "ADVANCE_ANIMATION animation_frame 1 4 100")
add_test(NAME debug_game_animation
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/physics_game.hyp --event FRAME)
set_tests_properties(debug_game_animation PROPERTIES PASS_REGULAR_EXPRESSION "advance animation animation_frame from 1 through 4 every 100 milliseconds")
add_test(NAME compile_game_media
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/game_media.hyp -o ${CMAKE_BINARY_DIR}/game_media.hyc)
if(SDL2_FOUND)
    add_test(NAME run_native_game_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 HYPERIAN_GAME_TEST_KEY=right HYPERIAN_VISUAL_TEST_STATE=player_x
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/blocks_game.hyc)
    set_tests_properties(run_native_game_headless PROPERTIES DEPENDS compile_native_game)
    set_tests_properties(run_native_game_headless PROPERTIES PASS_REGULAR_EXPRESSION "player_x=120")
    add_test(NAME run_game_timing_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_STATE=seconds_since_last_frame
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/blocks_game.hyc)
    set_tests_properties(run_game_timing_headless PROPERTIES DEPENDS compile_native_game PASS_REGULAR_EXPRESSION "seconds_since_last_frame=0\\.")
    add_test(NAME run_game_physics_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_STATE=player_hit
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/physics_game.hyc)
    set_tests_properties(run_game_physics_headless PROPERTIES DEPENDS compile_game_physics PASS_REGULAR_EXPRESSION "player_hit=true")
    add_test(NAME create_game_media_assets COMMAND hyperian_game_media_fixture)
    add_test(NAME run_game_media_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_VISUAL_TEST=1
            HYPERIAN_GAME_TEST_KEY=space HYPERIAN_VISUAL_TEST_STATE=player_y
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/game_media.hyc)
    set_tests_properties(run_game_media_headless PROPERTIES DEPENDS "compile_game_media;create_game_media_assets" PASS_REGULAR_EXPRESSION "player_y=80")
endif()
add_test(NAME compile_data_migrations
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/migrating_tasks.hyp -o ${CMAKE_BINARY_DIR}/migrating_tasks.hyc)
add_test(NAME inspect_data_migrations
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/migrating_tasks.hyc)
set_tests_properties(inspect_data_migrations PROPERTIES DEPENDS compile_data_migrations)

add_executable(hyperian_migration_fixture tests/migration_fixture.c)
target_compile_options(hyperian_migration_fixture PRIVATE -Wall -Wextra -Wpedantic)
add_executable(hyperian_game_media_fixture tests/game_media_fixture.c)
target_compile_options(hyperian_game_media_fixture PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME create_legacy_data
    COMMAND hyperian_migration_fixture create ${CMAKE_BINARY_DIR}/legacy-migration.hdb)
add_test(NAME migrate_legacy_data
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/legacy-migration.hdb
        $<TARGET_FILE:hyperian> migrate ${CMAKE_SOURCE_DIR}/examples/migrating_tasks.hyp)
set_tests_properties(migrate_legacy_data PROPERTIES DEPENDS create_legacy_data)
add_test(NAME verify_migrated_data
    COMMAND hyperian_migration_fixture verify ${CMAKE_BINARY_DIR}/legacy-migration.hdb)
set_tests_properties(verify_migrated_data PROPERTIES DEPENDS migrate_legacy_data)

if(SQLITE3_FOUND)
    add_executable(hyperian_sqlite_fixture tests/sqlite_fixture.c)
    target_link_libraries(hyperian_sqlite_fixture PRIVATE PkgConfig::SQLITE3)
    target_compile_options(hyperian_sqlite_fixture PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME compile_sqlite_storage
        COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/sqlite_tasks.hyp -o ${CMAKE_BINARY_DIR}/sqlite_tasks.hyc)
    add_test(NAME clean_native_sqlite_crud COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/native_crud.sqlite)
    add_test(NAME compile_native_sqlite_crud
        COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/native_crud_sqlite.hyp -o ${CMAKE_BINARY_DIR}/native_crud_sqlite.hyc)
    add_test(NAME run_native_sqlite_crud
        COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/native_crud.sqlite HYPERIAN_CONSOLE_TEST_STATE=sqlite_query_result
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/native_crud_sqlite.hyc)
    set_tests_properties(run_native_sqlite_crud PROPERTIES DEPENDS "clean_native_sqlite_crud;compile_native_sqlite_crud" PASS_REGULAR_EXPRESSION "sqlite_query_result=Updated SQLite title/3")
    add_test(NAME create_legacy_sqlite_data
        COMMAND hyperian_sqlite_fixture create ${CMAKE_BINARY_DIR}/legacy-migration.sqlite)
    add_test(NAME migrate_sqlite_data
        COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/legacy-migration.sqlite
            $<TARGET_FILE:hyperian> migrate ${CMAKE_SOURCE_DIR}/examples/sqlite_tasks.hyp)
    set_tests_properties(migrate_sqlite_data PROPERTIES DEPENDS create_legacy_sqlite_data)
    add_test(NAME verify_migrated_sqlite_data
        COMMAND hyperian_sqlite_fixture verify ${CMAKE_BINARY_DIR}/legacy-migration.sqlite)
    set_tests_properties(verify_migrated_sqlite_data PROPERTIES DEPENDS migrate_sqlite_data)
endif()
