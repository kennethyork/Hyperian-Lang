cmake_minimum_required(VERSION 3.16)
project(Hyperian VERSION 0.9.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_executable(hyperian
    src/main.c
    src/compiler.c
    src/bytecode.c
    src/runtime.c
    src/security.c
)
target_include_directories(hyperian PRIVATE src)
target_compile_options(hyperian PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS hyperian RUNTIME DESTINATION bin)

enable_testing()
add_test(NAME compiler_help COMMAND hyperian help)
add_test(NAME compile_example
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/tasks.hyp -o ${CMAKE_BINARY_DIR}/tasks.hyc)
add_test(NAME inspect_example
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/tasks.hyc)
set_tests_properties(inspect_example PROPERTIES DEPENDS compile_example)
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
add_test(NAME format_source
    COMMAND hyperian format ${CMAKE_SOURCE_DIR}/examples/english_logic.hyp)
add_test(NAME english_language_tests
    COMMAND hyperian test ${CMAKE_SOURCE_DIR}/examples/english_logic.hyp)
add_test(NAME compile_multi_file_program
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/modular/app.hyp -o ${CMAKE_BINARY_DIR}/modular.hyc)
add_test(NAME compile_data_migrations
    COMMAND hyperian compile ${CMAKE_SOURCE_DIR}/examples/migrating_tasks.hyp -o ${CMAKE_BINARY_DIR}/migrating_tasks.hyc)
add_test(NAME inspect_data_migrations
    COMMAND hyperian inspect ${CMAKE_BINARY_DIR}/migrating_tasks.hyc)
set_tests_properties(inspect_data_migrations PROPERTIES DEPENDS compile_data_migrations)

add_executable(hyperian_migration_fixture tests/migration_fixture.c)
target_compile_options(hyperian_migration_fixture PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME create_legacy_data
    COMMAND hyperian_migration_fixture create ${CMAKE_BINARY_DIR}/legacy-migration.hdb)
add_test(NAME migrate_legacy_data
    COMMAND ${CMAKE_COMMAND} -E env HYPERIAN_DATA=${CMAKE_BINARY_DIR}/legacy-migration.hdb
        $<TARGET_FILE:hyperian> migrate ${CMAKE_SOURCE_DIR}/examples/migrating_tasks.hyp)
set_tests_properties(migrate_legacy_data PROPERTIES DEPENDS create_legacy_data)
add_test(NAME verify_migrated_data
    COMMAND hyperian_migration_fixture verify ${CMAKE_BINARY_DIR}/legacy-migration.hdb)
set_tests_properties(verify_migrated_data PROPERTIES DEPENDS migrate_legacy_data)
