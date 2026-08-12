cmake_minimum_required(VERSION 3.16)
project(Hyperian VERSION 0.51.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
option(HYPERIAN_ENABLE_SDL2_IMAGE "Enable PNG, JPEG, and WebP game images when SDL2_image is available" ON)

if(MSVC)
    set(HYPERIAN_WARNING_OPTIONS /W4)
else()
    set(HYPERIAN_WARNING_OPTIONS -Wall -Wextra -Wpedantic)
endif()

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
    src/platform.c
)
target_include_directories(hyperian PRIVATE src)
target_compile_options(hyperian PRIVATE ${HYPERIAN_WARNING_OPTIONS})
target_compile_definitions(hyperian PRIVATE HYPERIAN_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    if(HYPERIAN_ENABLE_SDL2_IMAGE)
        pkg_check_modules(SDL2_IMAGE QUIET IMPORTED_TARGET SDL2_image)
    endif()
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
if(SDL2_FOUND AND HYPERIAN_ENABLE_SDL2_IMAGE AND SDL2_IMAGE_FOUND)
    target_compile_definitions(hyperian PRIVATE HYPERIAN_HAVE_SDL2_IMAGE=1)
    target_link_libraries(hyperian PRIVATE PkgConfig::SDL2_IMAGE)
endif()

install(TARGETS hyperian RUNTIME DESTINATION bin)
install(FILES README.md RUNNING.md DESTINATION .)

add_library(hyperian_mobile_runtime STATIC
    src/mobile.c src/runtime.c src/bytecode.c src/network.c src/security.c src/platform.c src/desktop.c src/game.c)
set_target_properties(hyperian_mobile_runtime PROPERTIES OUTPUT_NAME hyperian_mobile POSITION_INDEPENDENT_CODE ON)
target_include_directories(hyperian_mobile_runtime PUBLIC src)
target_compile_options(hyperian_mobile_runtime PRIVATE ${HYPERIAN_WARNING_OPTIONS})
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
if(SDL2_FOUND AND HYPERIAN_ENABLE_SDL2_IMAGE AND SDL2_IMAGE_FOUND)
    target_compile_definitions(hyperian_mobile_runtime PRIVATE HYPERIAN_HAVE_SDL2_IMAGE=1)
    target_link_libraries(hyperian_mobile_runtime PRIVATE PkgConfig::SDL2_IMAGE)
endif()
install(TARGETS hyperian_mobile_runtime ARCHIVE DESTINATION lib)
install(FILES src/hyperian.h DESTINATION include/hyperian)
install(DIRECTORY platform/android DESTINATION share/hyperian/platform)
install(DIRECTORY platform/ios DESTINATION share/hyperian/platform)
install(DIRECTORY platform/sqlite DESTINATION share/hyperian/platform)
install(FILES src/mobile.c src/runtime.c src/bytecode.c src/network.c src/security.c src/security.h src/platform.c src/platform.h src/desktop.c src/game.c src/hyperian.h
    DESTINATION share/hyperian/runtime)

if(WIN32)
    target_link_libraries(hyperian PRIVATE ws2_32 bcrypt)
    target_link_libraries(hyperian_mobile_runtime PRIVATE ws2_32 bcrypt)
endif()

enable_testing()
add_test(NAME compiler_help COMMAND hyperian help)
add_test(NAME run_source_help COMMAND hyperian help run)
set_tests_properties(run_source_help PROPERTIES PASS_REGULAR_EXPRESSION "You do not need a .hyr file")
add_test(NAME run_without_source_shows_help
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "Your .hyp file contains the left-aligned English MVC source."
        $<TARGET_FILE:hyperian> run)
add_test(NAME reject_running_runtime_pack
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "a .hyr file is a native runtime pack, not an application"
        $<TARGET_FILE:hyperian> run RuntimePack.hyr)
add_test(NAME reject_running_unknown_file
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "run a Hyperian .hyp source file or a compiled .hyc bytecode file"
        $<TARGET_FILE:hyperian> run application.txt)
add_test(NAME run_source_without_command
    COMMAND hyperian ${CMAKE_SOURCE_DIR}/examples/left_aligned_console.hyp)
set_tests_properties(run_source_without_command PROPERTIES PASS_REGULAR_EXPRESSION "Hello from left aligned Hyperian")
add_test(NAME toolchain_doctor COMMAND hyperian doctor)
set_tests_properties(toolchain_doctor PROPERTIES PASS_REGULAR_EXPRESSION "compiler and bytecode VM: ready")
add_test(NAME clean_installed_toolchain
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/InstalledToolchain)
add_test(NAME install_toolchain_for_users
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG> --prefix ${CMAKE_BINARY_DIR}/InstalledToolchain)
set_tests_properties(install_toolchain_for_users PROPERTIES DEPENDS clean_installed_toolchain)
if(WIN32)
    set(HYPERIAN_INSTALLED_EXECUTABLE ${CMAKE_BINARY_DIR}/InstalledToolchain/bin/hyperian.exe)
else()
    set(HYPERIAN_INSTALLED_EXECUTABLE ${CMAKE_BINARY_DIR}/InstalledToolchain/bin/hyperian)
endif()
add_test(NAME verify_installed_toolchain
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/installed_toolchain_test.sh ${CMAKE_BINARY_DIR}/InstalledToolchain
        ${HYPERIAN_INSTALLED_EXECUTABLE} ${CMAKE_SOURCE_DIR}/examples/left_aligned_console.hyp ${PROJECT_VERSION})
set_tests_properties(verify_installed_toolchain PROPERTIES DEPENDS install_toolchain_for_users)
add_test(NAME compile_example
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/tasks.hyp -o ${CMAKE_BINARY_DIR}/tasks.hyc)
add_test(NAME inspect_example
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/tasks.hyc)
set_tests_properties(inspect_example PROPERTIES DEPENDS compile_example)
add_test(NAME compile_indentation_example
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/tests/legacy_indentation.hyp -o ${CMAKE_BINARY_DIR}/indentation_console.hyc)
add_test(NAME run_indentation_example
    COMMAND hyperian run ${CMAKE_BINARY_DIR}/indentation_console.hyc)
set_tests_properties(run_indentation_example PROPERTIES DEPENDS compile_indentation_example PASS_REGULAR_EXPRESSION "Hello from indented Hyperian")
add_test(NAME format_indentation_example
    COMMAND hyperian format ${CMAKE_SOURCE_DIR}/tests/legacy_indentation.hyp)
set_tests_properties(format_indentation_example PROPERTIES PASS_REGULAR_EXPRESSION "that is all")
add_test(NAME reject_inconsistent_indentation
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "use the same indentation for instructions in this block"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_inconsistent_indentation.hyp)
add_test(NAME reject_tab_indentation
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "use spaces for indentation, not tabs"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_tab_indentation.hyp)
add_test(NAME compile_left_aligned_example
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/left_aligned_console.hyp -o ${CMAKE_BINARY_DIR}/left_aligned_console.hyc)
add_test(NAME run_left_aligned_example
    COMMAND hyperian run ${CMAKE_BINARY_DIR}/left_aligned_console.hyc)
set_tests_properties(run_left_aligned_example PROPERTIES DEPENDS compile_left_aligned_example PASS_REGULAR_EXPRESSION "Hello from left aligned Hyperian")
add_test(NAME format_left_aligned_example
    COMMAND hyperian format ${CMAKE_SOURCE_DIR}/examples/left_aligned_console.hyp)
set_tests_properties(format_left_aligned_example PROPERTIES PASS_REGULAR_EXPRESSION "that is all")
add_test(NAME rewrite_legacy_source_as_left_aligned
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/format_write_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/tests/legacy_indentation.hyp ${CMAKE_BINARY_DIR}/formatted_left_aligned.hyp)
add_test(NAME reject_non_left_aligned_english
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "start every Hyperian sentence at the left edge"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_left_alignment.hyp)
add_test(NAME reject_missing_english_closing_phrase
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "finish this block by saying: that is all"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_missing_closing_phrase.hyp)
add_test(NAME reject_unclosed_view_layout
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "finish this block by saying: that is all"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_unclosed_view_layout.hyp)
add_test(NAME reject_table_row_outside_table
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "a table row must be inside a table"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_table_row_outside_table.hyp)
add_test(NAME reject_table_cell_outside_row
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "a table cell must be directly inside a table row"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_table_cell_outside_row.hyp)
add_test(NAME reject_late_table_heading
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "put every table heading before the table rows"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_late_table_heading.hyp)
add_test(NAME inspect_and_format_view_choices
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/view_choice_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp ${CMAKE_BINARY_DIR}/view_choices.hyc)
add_test(NAME run_console_choice
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/console_choice_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/console_choice.hyp)
add_test(NAME reject_choice_without_options
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "a choice list must offer at least one choice"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_choice_without_options.hyp)
add_test(NAME reject_choice_option_outside_choice
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "an offered choice must be directly inside a choice list"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_choice_option_outside_choice.hyp)
add_test(NAME reject_duplicate_choice_value
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "each offered choice needs a different value"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_duplicate_choice_value.hyp)
add_test(NAME compile_pwa
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/pwa_tasks/app.hyp -o ${CMAKE_BINARY_DIR}/pwa_tasks.hyc)
add_test(NAME reject_pwa_without_route
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_pwa_route.hyp)
set_tests_properties(reject_pwa_without_route PROPERTIES WILL_FAIL TRUE)
find_program(CURL_EXECUTABLE curl)
find_program(FFMPEG_EXECUTABLE ffmpeg)
if(CURL_EXECUTABLE)
    add_test(NAME run_pwa_runtime
        COMMAND sh ${CMAKE_SOURCE_DIR}/tests/pwa_runtime_test.sh $<TARGET_FILE:hyperian> ${CMAKE_SOURCE_DIR}/examples/pwa_tasks/app.hyp ${CMAKE_BINARY_DIR})
    add_test(NAME run_english_names_web
        COMMAND sh ${CMAKE_SOURCE_DIR}/tests/english_names_web_test.sh $<TARGET_FILE:hyperian> ${CMAKE_SOURCE_DIR}/examples/english_names_web.hyp ${CMAKE_BINARY_DIR})
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
target_compile_options(hyperian_security_test PRIVATE ${HYPERIAN_WARNING_OPTIONS})
if(WIN32)
    target_link_libraries(hyperian_security_test PRIVATE bcrypt)
endif()
add_test(NAME native_security COMMAND hyperian_security_test ${CMAKE_SOURCE_DIR}/tests/sha256_fixture.txt)
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
    COMMAND hyperian build ${CMAKE_SOURCE_DIR}/examples/report_service.hyp -o ${CMAKE_BINARY_DIR}/standalone_report${CMAKE_EXECUTABLE_SUFFIX})
add_test(NAME run_standalone_service
    COMMAND ${CMAKE_BINARY_DIR}/standalone_report${CMAKE_EXECUTABLE_SUFFIX})
set_tests_properties(run_standalone_service PROPERTIES DEPENDS build_standalone_service)
add_test(NAME native_platform_name COMMAND hyperian platform)
set_tests_properties(native_platform_name PROPERTIES PASS_REGULAR_EXPRESSION "^(linux|macos|freebsd|windows)-(x64|arm64)")
add_test(NAME clean_native_runtime_pack
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/NativeRuntime)
add_test(NAME create_native_runtime_pack
    COMMAND hyperian pack runtime to ${CMAKE_BINARY_DIR}/NativeRuntime)
set_tests_properties(create_native_runtime_pack PROPERTIES DEPENDS clean_native_runtime_pack
    PASS_REGULAR_EXPRESSION "Packed the native Hyperian runtime")
add_test(NAME reject_existing_native_runtime_pack
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "already exists"
        $<TARGET_FILE:hyperian> pack runtime to ${CMAKE_BINARY_DIR}/NativeRuntime)
set_tests_properties(reject_existing_native_runtime_pack PROPERTIES DEPENDS create_native_runtime_pack)
add_test(NAME clean_native_runtime_archive
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/hyperian-${PROJECT_VERSION}-runtime.hyr)
add_test(NAME create_native_runtime_archive
    COMMAND hyperian pack runtime as ${CMAKE_BINARY_DIR}/hyperian-${PROJECT_VERSION}-runtime.hyr)
set_tests_properties(create_native_runtime_archive PROPERTIES DEPENDS clean_native_runtime_archive
    PASS_REGULAR_EXPRESSION "Packed the native Hyperian runtime")
add_test(NAME reject_existing_native_runtime_archive
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "already exists"
        $<TARGET_FILE:hyperian> pack runtime as ${CMAKE_BINARY_DIR}/hyperian-${PROJECT_VERSION}-runtime.hyr)
set_tests_properties(reject_existing_native_runtime_archive PROPERTIES DEPENDS create_native_runtime_archive)
add_test(NAME build_with_native_runtime_archive
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/report_service.hyp ${CMAKE_BINARY_DIR}/hyperian-${PROJECT_VERSION}-runtime.hyr
        ${CMAKE_BINARY_DIR}/runtime_archive_report)
set_tests_properties(build_with_native_runtime_archive PROPERTIES DEPENDS create_native_runtime_archive
    PASS_REGULAR_EXPRESSION "Daily service report")
add_test(NAME clean_native_runtime_archive_rejections
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/RuntimeArchiveRejections)
add_test(NAME reject_damaged_native_runtime_archives
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_archive_rejection_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/report_service.hyp ${CMAKE_BINARY_DIR}/hyperian-${PROJECT_VERSION}-runtime.hyr
        ${CMAKE_BINARY_DIR}/RuntimeArchiveRejections)
set_tests_properties(reject_damaged_native_runtime_archives PROPERTIES
    DEPENDS "create_native_runtime_archive;clean_native_runtime_archive_rejections")
add_test(NAME build_with_native_runtime_pack
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/report_service.hyp ${CMAKE_BINARY_DIR}/NativeRuntime ${CMAKE_BINARY_DIR}/runtime_pack_report)
set_tests_properties(build_with_native_runtime_pack PROPERTIES DEPENDS create_native_runtime_pack
    PASS_REGULAR_EXPRESSION "Daily service report")
add_test(NAME reject_wrong_native_runtime_pack
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_wrong_platform_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/report_service.hyp ${CMAKE_BINARY_DIR}/NativeRuntime ${CMAKE_BINARY_DIR}/wrong_runtime_report)
set_tests_properties(reject_wrong_native_runtime_pack PROPERTIES DEPENDS create_native_runtime_pack)
add_test(NAME clean_stale_native_runtime_pack
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/StaleNativeRuntime)
add_test(NAME reject_stale_native_runtime_pack
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_stale_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/report_service.hyp ${CMAKE_BINARY_DIR}/NativeRuntime
        ${CMAKE_BINARY_DIR}/StaleNativeRuntime ${CMAKE_BINARY_DIR}/stale_runtime_report)
set_tests_properties(reject_stale_native_runtime_pack PROPERTIES DEPENDS "create_native_runtime_pack;clean_stale_native_runtime_pack")
add_test(NAME clean_tampered_native_runtime_pack
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/TamperedNativeRuntime)
add_test(NAME reject_tampered_native_runtime_pack
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_tampered_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/report_service.hyp ${CMAKE_BINARY_DIR}/NativeRuntime
        ${CMAKE_BINARY_DIR}/TamperedNativeRuntime ${CMAKE_BINARY_DIR}/tampered_runtime_report)
set_tests_properties(reject_tampered_native_runtime_pack PROPERTIES DEPENDS "create_native_runtime_pack;clean_tampered_native_runtime_pack")
add_test(NAME clean_cross_platform_console_bundle
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CrossPlatformConsoleBundle)
add_test(NAME clean_cross_platform_bundle_work
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CrossPlatformBundleWork)
add_test(NAME build_cross_platform_console_bundle
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_bundle_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/bundle_console/app.hyp ${CMAKE_BINARY_DIR}/NativeRuntime
        ${CMAKE_BINARY_DIR}/CrossPlatformConsoleBundle ${CMAKE_BINARY_DIR}/CrossPlatformBundleWork)
set_tests_properties(build_cross_platform_console_bundle PROPERTIES
    DEPENDS "create_native_runtime_pack;clean_cross_platform_console_bundle;clean_cross_platform_bundle_work"
    PASS_REGULAR_EXPRESSION "Hello from a bundled asset")
add_test(NAME clean_runtime_archive_console_bundle
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/RuntimeArchiveConsoleBundle)
add_test(NAME clean_runtime_archive_bundle_work
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/RuntimeArchiveBundleWork)
add_test(NAME build_console_bundle_from_runtime_archive
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_bundle_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/bundle_console/app.hyp ${CMAKE_BINARY_DIR}/hyperian-${PROJECT_VERSION}-runtime.hyr
        ${CMAKE_BINARY_DIR}/RuntimeArchiveConsoleBundle ${CMAKE_BINARY_DIR}/RuntimeArchiveBundleWork)
set_tests_properties(build_console_bundle_from_runtime_archive PROPERTIES
    DEPENDS "create_native_runtime_archive;clean_runtime_archive_console_bundle;clean_runtime_archive_bundle_work"
    PASS_REGULAR_EXPRESSION "Hello from a bundled asset")
add_test(NAME clean_cross_platform_web_bundle
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CrossPlatformWebBundle)
add_test(NAME build_cross_platform_web_bundle
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_web_bundle_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/pwa_tasks/app.hyp ${CMAKE_BINARY_DIR}/NativeRuntime
        ${CMAKE_BINARY_DIR}/CrossPlatformWebBundle)
set_tests_properties(build_cross_platform_web_bundle PROPERTIES
    DEPENDS "create_native_runtime_pack;clean_cross_platform_web_bundle")
add_test(NAME clean_cross_platform_bundle_rejections
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/DamagedBundleRuntime)
add_test(NAME reject_invalid_cross_platform_bundles
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/runtime_pack_bundle_rejection_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/bundle_console/app.hyp ${CMAKE_BINARY_DIR}/NativeRuntime
        ${CMAKE_BINARY_DIR}/WrongPlatformBundle ${CMAKE_BINARY_DIR}/DamagedBundleRuntime
        ${CMAKE_BINARY_DIR}/DamagedRuntimeBundle)
set_tests_properties(reject_invalid_cross_platform_bundles PROPERTIES
    DEPENDS "create_native_runtime_pack;clean_cross_platform_bundle_rejections")
add_test(NAME clean_failed_cross_platform_bundle
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/UnsafeBundleProject)
add_test(NAME reject_unsafe_bundle_asset_and_clean_output
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/bundle_cleanup_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/bundle_console/app.hyp ${CMAKE_BINARY_DIR}/NativeRuntime
        ${CMAKE_BINARY_DIR}/UnsafeBundleProject ${CMAKE_BINARY_DIR}/UnsafeBundleOutput)
set_tests_properties(reject_unsafe_bundle_asset_and_clean_output PROPERTIES
    DEPENDS "create_native_runtime_pack;clean_failed_cross_platform_bundle")
add_test(NAME clean_console_bundle COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/BundleConsole)
add_test(NAME build_console_bundle
    COMMAND hyperian bundle ${CMAKE_SOURCE_DIR}/examples/bundle_console/app.hyp -o ${CMAKE_BINARY_DIR}/BundleConsole)
set_tests_properties(build_console_bundle PROPERTIES DEPENDS clean_console_bundle)
add_test(NAME verify_console_bundle_manifest
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/bundle_manifest_test.sh $<TARGET_FILE:hyperian> ${CMAKE_BINARY_DIR}/BundleConsole)
set_tests_properties(verify_console_bundle_manifest PROPERTIES DEPENDS build_console_bundle)
add_test(NAME run_console_bundle COMMAND ${CMAKE_BINARY_DIR}/BundleConsole/run${CMAKE_EXECUTABLE_SUFFIX})
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
add_test(NAME compile_english_names
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/english_names.hyp -o ${CMAKE_BINARY_DIR}/english_names.hyc)
add_test(NAME run_english_names
    COMMAND hyperian run ${CMAKE_SOURCE_DIR}/examples/english_names.hyp)
set_tests_properties(run_english_names PROPERTIES PASS_REGULAR_EXPRESSION "Hello, Ada Lovelace")
add_test(NAME test_english_names
    COMMAND hyperian test ${CMAKE_SOURCE_DIR}/examples/english_names.hyp)
set_tests_properties(test_english_names PROPERTIES PASS_REGULAR_EXPRESSION "1 test, 0 failures")
add_test(NAME debug_english_names
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/english_names.hyp)
set_tests_properties(debug_english_names PROPERTIES PASS_REGULAR_EXPRESSION "visit count = 2")
add_test(NAME inspect_english_names
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/english_names.hyc)
set_tests_properties(inspect_english_names PROPERTIES DEPENDS compile_english_names PASS_REGULAR_EXPRESSION "SET_VALUE        visit count value called \"visit count\" plus 1")
add_test(NAME format_english_names
    COMMAND hyperian format ${CMAKE_SOURCE_DIR}/examples/english_names.hyp)
set_tests_properties(format_english_names PROPERTIES PASS_REGULAR_EXPRESSION "set \"visit count\" to value called \"visit count\" plus 1")
add_test(NAME reject_bad_english_value_name
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "a value name must start with a letter"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_english_value_name.hyp)
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
    add_test(NAME run_mobile_preview_choice_change_event
        COMMAND ${XVFB_RUN} -a ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/mobile_choice_events.hdb HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_INPUT_NAME=priority
            HYPERIAN_VISUAL_TEST_INPUT=high HYPERIAN_VISUAL_TEST_STATE=priority_status
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp)
    set_tests_properties(run_mobile_preview_choice_change_event PROPERTIES DEPENDS compile_mobile_preview PASS_REGULAR_EXPRESSION "priority_status=Priority:high")
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
add_test(NAME inspect_and_format_view_layouts
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/view_layout_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp ${CMAKE_BINARY_DIR}/view_layouts.hyc)
add_test(NAME inspect_and_format_view_tables
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/view_table_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp ${CMAKE_BINARY_DIR}/view_tables.hyc)
add_test(NAME run_console_table
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/console_table_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/console_table.hyp)
add_test(NAME clean_generated_english_mobile
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/GeneratedEnglishMobile)
add_test(NAME create_generated_english_mobile
    COMMAND $<TARGET_FILE:hyperian> new GeneratedEnglishMobile --target mobile)
set_tests_properties(create_generated_english_mobile PROPERTIES DEPENDS clean_generated_english_mobile WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME verify_generated_english_mobile
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/generated_native_names_test.sh ${CMAKE_BINARY_DIR}/GeneratedEnglishMobile $<TARGET_FILE:hyperian>)
set_tests_properties(verify_generated_english_mobile PROPERTIES DEPENDS create_generated_english_mobile)
add_test(NAME clean_generated_english_targets
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/GeneratedEnglishTargets)
add_test(NAME verify_generated_english_targets
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/generated_english_targets_test.sh
        ${CMAKE_BINARY_DIR}/GeneratedEnglishTargets $<TARGET_FILE:hyperian>)
set_tests_properties(verify_generated_english_targets PROPERTIES DEPENDS clean_generated_english_targets)
add_test(NAME compile_mobile_connected
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/mobile_connected.hyp -o ${CMAKE_BINARY_DIR}/mobile_connected.hyc)
add_executable(hyperian_mobile_bridge_test tests/mobile_bridge_test.c)
target_include_directories(hyperian_mobile_bridge_test PRIVATE src)
target_compile_options(hyperian_mobile_bridge_test PRIVATE ${HYPERIAN_WARNING_OPTIONS})
target_link_libraries(hyperian_mobile_bridge_test PRIVATE hyperian_mobile_runtime)
add_test(NAME clean_mobile_bridge_data COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/mobile_bridge.hdb)
add_test(NAME run_mobile_bridge
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/mobile_bridge.hdb
        $<TARGET_FILE:hyperian_mobile_bridge_test> ${CMAKE_BINARY_DIR}/mobile_tasks.hyc)
set_tests_properties(run_mobile_bridge PROPERTIES DEPENDS "compile_mobile_preview;clean_mobile_bridge_data")
if(SQLITE3_FOUND)
    add_executable(hyperian_mobile_connected_test tests/mobile_connected_test.c)
    target_include_directories(hyperian_mobile_connected_test PRIVATE src)
    target_compile_options(hyperian_mobile_connected_test PRIVATE ${HYPERIAN_WARNING_OPTIONS})
    target_link_libraries(hyperian_mobile_connected_test PRIVATE hyperian_mobile_runtime)
    add_test(NAME clean_mobile_connected_data COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/mobile_connected.sqlite)
    add_test(NAME run_mobile_connected
        COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/mobile_connected.sqlite
            $<TARGET_FILE:hyperian_mobile_connected_test> ${CMAKE_BINARY_DIR}/mobile_connected.hyc ${CMAKE_BINARY_DIR}/mobile_connected.sqlite)
    set_tests_properties(run_mobile_connected PROPERTIES DEPENDS "compile_mobile_connected;clean_mobile_connected_data")
endif()
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
add_test(NAME prepare_mobile_build_tests
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/fake-release.keystore)
add_test(NAME clean_android_apk_build
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/MobileTasks.apk ${CMAKE_BINARY_DIR}/MobileTasks-apk.log)
add_test(NAME build_signed_android_apk
    COMMAND ${CMAKE_COMMAND} -E env
        HYPERIAN_APPLICATION_ID=com.example.mobiletasks
        HYPERIAN_ANDROID_KEYSTORE=${CMAKE_BINARY_DIR}/fake-release.keystore
        HYPERIAN_ANDROID_KEY_ALIAS=release
        HYPERIAN_ANDROID_STORE_PASSWORD=store-secret
        HYPERIAN_ANDROID_KEY_PASSWORD=key-secret
        HYPERIAN_GRADLE=${CMAKE_SOURCE_DIR}/tests/fake_mobile_builder.sh
        HYPERIAN_FAKE_PLATFORM=android-apk
        HYPERIAN_FAKE_LOG=${CMAKE_BINARY_DIR}/MobileTasks-apk.log
        $<TARGET_FILE:hyperian> build ${CMAKE_SOURCE_DIR}/examples/mobile_connected.hyp for android as ${CMAKE_BINARY_DIR}/MobileTasks.apk)
set_tests_properties(build_signed_android_apk PROPERTIES DEPENDS "prepare_mobile_build_tests;clean_android_apk_build")
add_test(NAME verify_signed_android_apk
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/mobile_build_test.sh ${CMAKE_BINARY_DIR}/MobileTasks.apk apk ${CMAKE_BINARY_DIR}/MobileTasks-apk.log)
set_tests_properties(verify_signed_android_apk PROPERTIES DEPENDS build_signed_android_apk)
add_test(NAME clean_android_aab_build
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/MobileTasks.aab ${CMAKE_BINARY_DIR}/MobileTasks-aab.log)
add_test(NAME build_signed_android_aab
    COMMAND ${CMAKE_COMMAND} -E env
        HYPERIAN_APPLICATION_ID=com.example.mobiletasks
        HYPERIAN_ANDROID_KEYSTORE=${CMAKE_BINARY_DIR}/fake-release.keystore
        HYPERIAN_ANDROID_KEY_ALIAS=release
        HYPERIAN_ANDROID_STORE_PASSWORD=store-secret
        HYPERIAN_ANDROID_KEY_PASSWORD=key-secret
        HYPERIAN_GRADLE=${CMAKE_SOURCE_DIR}/tests/fake_mobile_builder.sh
        HYPERIAN_FAKE_PLATFORM=android-aab
        HYPERIAN_FAKE_LOG=${CMAKE_BINARY_DIR}/MobileTasks-aab.log
        $<TARGET_FILE:hyperian> build ${CMAKE_SOURCE_DIR}/examples/mobile_connected.hyp for android as ${CMAKE_BINARY_DIR}/MobileTasks.aab)
set_tests_properties(build_signed_android_aab PROPERTIES DEPENDS "prepare_mobile_build_tests;clean_android_aab_build")
add_test(NAME verify_signed_android_aab
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/mobile_build_test.sh ${CMAKE_BINARY_DIR}/MobileTasks.aab aab ${CMAKE_BINARY_DIR}/MobileTasks-aab.log)
set_tests_properties(verify_signed_android_aab PROPERTIES DEPENDS build_signed_android_aab)
add_test(NAME clean_ios_ipa_build
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/MobileTasks.ipa ${CMAKE_BINARY_DIR}/MobileTasks-ipa.log)
add_test(NAME build_signed_ios_ipa
    COMMAND ${CMAKE_COMMAND} -E env
        HYPERIAN_APPLICATION_ID=com.example.mobiletasks
        HYPERIAN_IOS_TEAM=ABCDE12345
        HYPERIAN_IOS_DISTRIBUTION=app-store-connect
        HYPERIAN_XCODEBUILD=${CMAKE_SOURCE_DIR}/tests/fake_mobile_builder.sh
        HYPERIAN_FAKE_PLATFORM=ios
        HYPERIAN_FAKE_LOG=${CMAKE_BINARY_DIR}/MobileTasks-ipa.log
        $<TARGET_FILE:hyperian> build ${CMAKE_SOURCE_DIR}/examples/mobile_connected.hyp for ios as ${CMAKE_BINARY_DIR}/MobileTasks.ipa)
set_tests_properties(build_signed_ios_ipa PROPERTIES DEPENDS clean_ios_ipa_build)
add_test(NAME verify_signed_ios_ipa
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/mobile_build_test.sh ${CMAKE_BINARY_DIR}/MobileTasks.ipa ipa ${CMAKE_BINARY_DIR}/MobileTasks-ipa.log)
set_tests_properties(verify_signed_ios_ipa PROPERTIES DEPENDS build_signed_ios_ipa)
add_test(NAME reject_mobile_build_extension
    COMMAND hyperian build ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp for android as ${CMAKE_BINARY_DIR}/MobileTasks.bin)
set_tests_properties(reject_mobile_build_extension PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_mobile_build_without_identity
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_APPLICATION_ID=
        $<TARGET_FILE:hyperian> build ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp for android as ${CMAKE_BINARY_DIR}/MissingIdentity.apk)
set_tests_properties(reject_mobile_build_without_identity PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_ios_build_without_team
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_APPLICATION_ID=com.example.mobiletasks HYPERIAN_IOS_TEAM=
        $<TARGET_FILE:hyperian> build ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp for ios as ${CMAKE_BINARY_DIR}/MissingTeam.ipa)
set_tests_properties(reject_ios_build_without_team PROPERTIES WILL_FAIL TRUE)
add_test(NAME preserve_existing_mobile_output
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/mobile_output_guard_test.sh $<TARGET_FILE:hyperian>
        ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp ${CMAKE_BINARY_DIR}/ProtectedMobile.apk
        ${CMAKE_BINARY_DIR}/fake-release.keystore ${CMAKE_SOURCE_DIR}/tests/fake_mobile_builder.sh)
set_tests_properties(preserve_existing_mobile_output PROPERTIES DEPENDS prepare_mobile_build_tests)
add_test(NAME reject_web_mobile_build
    COMMAND hyperian build ${CMAKE_SOURCE_DIR}/examples/tasks.hyp for android as ${CMAKE_BINARY_DIR}/WrongTarget.apk)
set_tests_properties(reject_web_mobile_build PROPERTIES WILL_FAIL TRUE)
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
set_tests_properties(format_native_events PROPERTIES PASS_REGULAR_EXPRESSION "when application pauses")
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
add_test(NAME clean_generated_circle_game
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/GeneratedCircleGame)
add_test(NAME create_generated_circle_game
    COMMAND $<TARGET_FILE:hyperian> new GeneratedCircleGame --target game)
set_tests_properties(create_generated_circle_game PROPERTIES DEPENDS clean_generated_circle_game WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME verify_generated_circle_game
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/generated_game_test.sh ${CMAKE_BINARY_DIR}/GeneratedCircleGame $<TARGET_FILE:hyperian>)
set_tests_properties(verify_generated_circle_game PROPERTIES DEPENDS create_generated_circle_game)
add_test(NAME compile_game_physics
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/physics_game.hyp -o ${CMAKE_BINARY_DIR}/physics_game.hyc)
add_executable(hyperian_game_physics_test tests/game_physics_test.c)
target_include_directories(hyperian_game_physics_test PRIVATE src)
target_compile_options(hyperian_game_physics_test PRIVATE ${HYPERIAN_WARNING_OPTIONS})
target_link_libraries(hyperian_game_physics_test PRIVATE hyperian_mobile_runtime)
add_test(NAME run_game_physics
    COMMAND hyperian_game_physics_test ${CMAKE_BINARY_DIR}/physics_game.hyc)
set_tests_properties(run_game_physics PROPERTIES DEPENDS compile_game_physics)
add_test(NAME inspect_circle_collision
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/physics_game.hyc)
set_tests_properties(inspect_circle_collision PROPERTIES DEPENDS compile_game_physics PASS_REGULAR_EXPRESSION "CHECK_CIRCLE_COLLISION ball horizontal center ball vertical center ball radius coin horizontal center coin vertical center coin radius coin hit")
add_test(NAME debug_circle_collision
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/physics_game.hyp --event FRAME)
set_tests_properties(debug_circle_collision PROPERTIES PASS_REGULAR_EXPRESSION "check whether circle centered at ball horizontal center ball vertical center with radius ball radius touches circle centered at coin horizontal center coin vertical center with radius coin radius as coin hit")
add_test(NAME inspect_line_collision
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/physics_game.hyc)
set_tests_properties(inspect_line_collision PROPERTIES DEPENDS compile_game_physics PASS_REGULAR_EXPRESSION "CHECK_LINE_COLLISION 0 0 10 10 0 10 10 0 crossing lines touch")
add_test(NAME debug_line_collision
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/physics_game.hyp --event FRAME)
set_tests_properties(debug_line_collision PROPERTIES PASS_REGULAR_EXPRESSION "check whether line from 0 0 to 10 10 touches line from 0 10 to 10 0 as crossing lines touch")
add_test(NAME inspect_polygon_collision
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/physics_game.hyc)
set_tests_properties(inspect_polygon_collision PROPERTIES DEPENDS compile_game_physics PASS_REGULAR_EXPRESSION "CHECK_POLYGON_COLLISION 0,0;20,0;20,20;0,20 5,5;10,5;5,10 polygon contains polygon")
add_test(NAME debug_polygon_collision
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/physics_game.hyp --event FRAME)
set_tests_properties(debug_polygon_collision PROPERTIES PASS_REGULAR_EXPRESSION "check whether polygon through 0 0 then 20 0 then 20 20 then 0 20 touches polygon through 5 5 then 10 5 then 5 10 as polygon contains polygon")
add_test(NAME compile_maximum_polygon
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/tests/polygon_max_points.hyp -o ${CMAKE_BINARY_DIR}/polygon_max_points.hyc)
add_test(NAME preserve_legacy_rectangle_collision
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/tests/legacy_rectangle_collision.hyp --event FRAME)
set_tests_properties(preserve_legacy_rectangle_collision PROPERTIES PASS_REGULAR_EXPRESSION "touched = true")
add_test(NAME reject_physics_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_physics_target.hyp)
set_tests_properties(reject_physics_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_circle_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_circle_target.hyp)
set_tests_properties(reject_circle_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_line_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_line_target.hyp)
set_tests_properties(reject_line_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_line_collision_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_line_collision_target.hyp)
set_tests_properties(reject_line_collision_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_polygon_collision_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_polygon_target.hyp)
set_tests_properties(reject_polygon_collision_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_polygon_drawing_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_polygon_drawing_target.hyp)
set_tests_properties(reject_polygon_drawing_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_polygon_with_too_few_points
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "a polygon needs at least three points"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_polygon_points.hyp)
add_test(NAME reject_polygon_with_too_many_points
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "a polygon can have at most 32 points"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_polygon_limit.hyp)
add_test(NAME reject_game_drawing_outside_game
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_game_drawing_target.hyp)
set_tests_properties(reject_game_drawing_outside_game PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_negative_circle_radius
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "circle radius cannot be negative"
        $<TARGET_FILE:hyperian> debug ${CMAKE_SOURCE_DIR}/tests/bad_circle_radius.hyp --event FRAME)
add_test(NAME reject_negative_line_circle_radius
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "line and circle collision radius cannot be negative"
        $<TARGET_FILE:hyperian> debug ${CMAKE_SOURCE_DIR}/tests/bad_line_circle_radius.hyp --event FRAME)
add_test(NAME reject_negative_line_rectangle_size
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "line and rectangle collision size cannot be negative"
        $<TARGET_FILE:hyperian> debug ${CMAKE_SOURCE_DIR}/tests/bad_line_rectangle_size.hyp --event FRAME)
add_test(NAME reject_negative_polygon_circle_radius
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "polygon and circle collision radius cannot be negative"
        $<TARGET_FILE:hyperian> debug ${CMAKE_SOURCE_DIR}/tests/bad_polygon_circle_radius.hyp --event FRAME)
add_test(NAME reject_negative_polygon_rectangle_size
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "polygon and rectangle collision size cannot be negative"
        $<TARGET_FILE:hyperian> debug ${CMAKE_SOURCE_DIR}/tests/bad_polygon_rectangle_size.hyp --event FRAME)
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
set_tests_properties(inspect_game_animation PROPERTIES DEPENDS compile_game_physics PASS_REGULAR_EXPRESSION "ADVANCE_ANIMATION animation frame 1 4 100")
add_test(NAME debug_game_animation
    COMMAND hyperian debug ${CMAKE_SOURCE_DIR}/examples/physics_game.hyp --event FRAME)
set_tests_properties(debug_game_animation PROPERTIES PASS_REGULAR_EXPRESSION "advance animation animation frame from 1 through 4 every 100 milliseconds")
add_test(NAME compile_game_media
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/game_media.hyp -o ${CMAKE_BINARY_DIR}/game_media.hyc)
add_test(NAME compile_game_audio
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/game_audio.hyp -o ${CMAKE_BINARY_DIR}/game_audio.hyc)
add_test(NAME inspect_game_audio
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/game_audio.hyc)
set_tests_properties(inspect_game_audio PROPERTIES DEPENDS compile_game_audio
    PASS_REGULAR_EXPRESSION "PLAY_MUSIC.*repeatedly")
add_test(NAME inspect_game_audio_volume
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/game_audio.hyc)
set_tests_properties(inspect_game_audio_volume PROPERTIES DEPENDS compile_game_audio
    PASS_REGULAR_EXPRESSION "SET_MUSIC_VOLUME 40")
add_test(NAME inspect_game_audio_controls
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/game_audio.hyc)
set_tests_properties(inspect_game_audio_controls PROPERTIES DEPENDS compile_game_audio
    PASS_REGULAR_EXPRESSION "PAUSE_MUSIC")
add_test(NAME inspect_game_audio_resume
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/game_audio.hyc)
set_tests_properties(inspect_game_audio_resume PROPERTIES DEPENDS compile_game_audio
    PASS_REGULAR_EXPRESSION "RESUME_MUSIC")
add_test(NAME inspect_game_audio_stop
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/game_audio.hyc)
set_tests_properties(inspect_game_audio_stop PROPERTIES DEPENDS compile_game_audio
    PASS_REGULAR_EXPRESSION "STOP_MUSIC")
add_test(NAME reject_audio_outside_game
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "audio instructions require an application declared as game"
        $<TARGET_FILE:hyperian> check ${CMAKE_SOURCE_DIR}/tests/bad_audio_target.hyp)
add_test(NAME compile_game_common_images
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/game_common_images.hyp -o ${CMAKE_BINARY_DIR}/game_common_images.hyc)
if(SDL2_FOUND)
    add_test(NAME doctor_without_game_mixer
        COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DISABLE_SDL2_MIXER=1 $<TARGET_FILE:hyperian> doctor)
    set_tests_properties(doctor_without_game_mixer PROPERTIES
        PASS_REGULAR_EXPRESSION "SDL2_mixer.*not installed; WAV effects are ready")
    add_test(NAME run_native_game_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 HYPERIAN_GAME_TEST_KEY=right HYPERIAN_VISUAL_TEST_STATE=player_x
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/blocks_game.hyc)
    set_tests_properties(run_native_game_headless PROPERTIES DEPENDS compile_native_game)
    set_tests_properties(run_native_game_headless PROPERTIES PASS_REGULAR_EXPRESSION "player_x=120")
    add_test(NAME run_game_timing_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 "HYPERIAN_VISUAL_TEST_STATE=seconds since last frame"
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/blocks_game.hyc)
    set_tests_properties(run_game_timing_headless PROPERTIES DEPENDS compile_native_game PASS_REGULAR_EXPRESSION "seconds since last frame=0\\.")
    add_test(NAME run_game_physics_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 "HYPERIAN_VISUAL_TEST_STATE=player hit"
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/physics_game.hyc)
    set_tests_properties(run_game_physics_headless PROPERTIES DEPENDS compile_game_physics PASS_REGULAR_EXPRESSION "player hit=true")
    add_test(NAME run_maximum_polygon_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/polygon_max_points.hyc)
    set_tests_properties(run_maximum_polygon_headless PROPERTIES DEPENDS compile_maximum_polygon)
    add_test(NAME create_game_media_assets COMMAND hyperian_game_media_fixture)
    add_test(NAME run_game_media_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_VISUAL_TEST=1
            HYPERIAN_GAME_TEST_KEY=space "HYPERIAN_VISUAL_TEST_STATE=player top"
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/game_media.hyc)
    set_tests_properties(run_game_media_headless PROPERTIES DEPENDS "compile_game_media;create_game_media_assets" PASS_REGULAR_EXPRESSION "player top=80")
    add_test(NAME run_wav_game_audio_without_mixer
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_DISABLE_SDL2_MIXER=1
            HYPERIAN_VISUAL_TEST=1 HYPERIAN_GAME_TEST_KEY=space "HYPERIAN_VISUAL_TEST_STATE=player top"
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/game_media.hyc)
    set_tests_properties(run_wav_game_audio_without_mixer PROPERTIES DEPENDS "compile_game_media;create_game_media_assets"
        PASS_REGULAR_EXPRESSION "player top=80")
    add_test(NAME reject_compressed_sound_without_mixer
        COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "needs SDL2_mixer to play compressed sound"
            ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_DISABLE_SDL2_MIXER=1
            HYPERIAN_VISUAL_TEST=1 $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/game_audio.hyc)
    set_tests_properties(reject_compressed_sound_without_mixer PROPERTIES DEPENDS compile_game_audio)
    add_test(NAME reject_streaming_music_without_mixer
        COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "needs SDL2_mixer to stream music"
            ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_DISABLE_SDL2_MIXER=1
            HYPERIAN_VISUAL_TEST=1 $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/tests/bad_game_music.hyp)
    add_test(NAME reject_bad_music_volume
        COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "music volume must be a number from 0 through 100 percent"
            ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_VISUAL_TEST=1
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/tests/bad_music_volume.hyp)
    if(FFMPEG_EXECUTABLE)
        add_test(NAME run_compressed_game_audio_headless
            COMMAND sh ${CMAKE_SOURCE_DIR}/tests/game_audio_runtime_test.sh $<TARGET_FILE:hyperian>
                ${FFMPEG_EXECUTABLE} ${CMAKE_SOURCE_DIR}/examples/game_audio.hyp ${CMAKE_BINARY_DIR}/game_audio_runtime.hyc)
        set_tests_properties(run_compressed_game_audio_headless PROPERTIES SKIP_RETURN_CODE 77
            PASS_REGULAR_EXPRESSION "audio ready=yes")
    endif()
    add_test(NAME reject_broken_streaming_music
        COMMAND sh ${CMAKE_SOURCE_DIR}/tests/require_mixer_test.sh $<TARGET_FILE:hyperian>
            sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "could not load music assets/broken.ogg"
            ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_VISUAL_TEST=1
            $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/tests/bad_game_music.hyp)
    set_tests_properties(reject_broken_streaming_music PROPERTIES DEPENDS create_game_media_assets SKIP_RETURN_CODE 77)
    add_test(NAME run_legacy_game_frame_name_headless
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 "HYPERIAN_VISUAL_TEST_STATE=legacy frame time"
            $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/game_media.hyc)
    set_tests_properties(run_legacy_game_frame_name_headless PROPERTIES DEPENDS "compile_game_media;create_game_media_assets" PASS_REGULAR_EXPRESSION "legacy frame time=0\\.")
    if(HYPERIAN_ENABLE_SDL2_IMAGE AND SDL2_IMAGE_FOUND)
        add_test(NAME doctor_common_game_images COMMAND hyperian doctor)
        set_tests_properties(doctor_common_game_images PROPERTIES PASS_REGULAR_EXPRESSION "PNG, JPEG, and WebP game images .*: ready")
        add_test(NAME run_game_common_images_headless
            COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1 "HYPERIAN_VISUAL_TEST_STATE=images loaded"
                $<TARGET_FILE:hyperian> run ${CMAKE_BINARY_DIR}/game_common_images.hyc)
        set_tests_properties(run_game_common_images_headless PROPERTIES DEPENDS "compile_game_common_images;create_game_media_assets" PASS_REGULAR_EXPRESSION "images loaded=yes")
        add_test(NAME reject_broken_game_image
            COMMAND sh ${CMAKE_SOURCE_DIR}/tests/reject_with_message_test.sh "could not load game image assets/broken.png"
                ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy HYPERIAN_VISUAL_TEST=1
                $<TARGET_FILE:hyperian> run ${CMAKE_SOURCE_DIR}/tests/bad_game_image.hyp)
        set_tests_properties(reject_broken_game_image PROPERTIES DEPENDS create_game_media_assets)
    endif()
endif()
add_test(NAME compile_data_migrations
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/migrating_tasks.hyp -o ${CMAKE_BINARY_DIR}/migrating_tasks.hyc)
add_test(NAME inspect_data_migrations
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/migrating_tasks.hyc)
set_tests_properties(inspect_data_migrations PROPERTIES DEPENDS compile_data_migrations)

add_executable(hyperian_migration_fixture tests/migration_fixture.c)
target_compile_options(hyperian_migration_fixture PRIVATE ${HYPERIAN_WARNING_OPTIONS})
add_executable(hyperian_game_media_fixture tests/game_media_fixture.c)
target_compile_options(hyperian_game_media_fixture PRIVATE ${HYPERIAN_WARNING_OPTIONS})
add_executable(hyperian_embedded_sqlite_test tests/embedded_sqlite_test.c platform/sqlite/sqlite3.c)
target_include_directories(hyperian_embedded_sqlite_test PRIVATE platform/sqlite)
target_compile_definitions(hyperian_embedded_sqlite_test PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1 SQLITE_DEFAULT_FOREIGN_KEYS=1)
target_compile_options(hyperian_embedded_sqlite_test PRIVATE ${HYPERIAN_WARNING_OPTIONS})
set_source_files_properties(platform/sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "-w")
add_test(NAME embedded_android_sqlite COMMAND hyperian_embedded_sqlite_test)
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
    target_compile_options(hyperian_sqlite_fixture PRIVATE ${HYPERIAN_WARNING_OPTIONS})
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
