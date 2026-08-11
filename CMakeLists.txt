cmake_minimum_required(VERSION 3.16)
project(Hyperian VERSION 0.17.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_executable(hyperian
    src/main.c
    src/compiler.c
    src/bytecode.c
    src/runtime.c
    src/desktop.c
    src/game.c
    src/network.c
    src/security.c
)
target_include_directories(hyperian PRIVATE src)
target_compile_options(hyperian PRIVATE -Wall -Wextra -Wpedantic)

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

enable_testing()
add_test(NAME compiler_help COMMAND hyperian help)
add_test(NAME compile_example
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/tasks.hyp -o ${CMAKE_BINARY_DIR}/tasks.hyc)
add_test(NAME inspect_example
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/tasks.hyc)
set_tests_properties(inspect_example PROPERTIES DEPENDS compile_example)
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
add_test(NAME reject_duplicate_field
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_duplicate_field.hyp)
set_tests_properties(reject_duplicate_field PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_crud_without_id
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_crud_route.hyp)
set_tests_properties(reject_crud_without_id PROPERTIES WILL_FAIL TRUE)
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
add_test(NAME build_standalone_service
    COMMAND hyperian build ${CMAKE_SOURCE_DIR}/examples/report_service.hyp -o ${CMAKE_BINARY_DIR}/standalone_report)
add_test(NAME run_standalone_service
    COMMAND ${CMAKE_BINARY_DIR}/standalone_report)
set_tests_properties(run_standalone_service PROPERTIES DEPENDS build_standalone_service)
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
add_test(NAME compile_native_desktop
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/desktop_notes.hyp -o ${CMAKE_BINARY_DIR}/desktop_notes.hyc)
add_test(NAME compile_mobile_preview
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/mobile_tasks.hyp -o ${CMAKE_BINARY_DIR}/mobile_tasks.hyc)
add_test(NAME reject_missing_desktop_action
    COMMAND hyperian check ${CMAKE_SOURCE_DIR}/tests/bad_desktop_action.hyp)
set_tests_properties(reject_missing_desktop_action PROPERTIES WILL_FAIL TRUE)
add_test(NAME compile_native_game
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/blocks_game.hyp -o ${CMAKE_BINARY_DIR}/blocks_game.hyc)
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
