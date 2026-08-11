#include <sqlite3.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    sqlite3 *database = NULL; sqlite3_stmt *statement = NULL;
    int okay = !strcmp(sqlite3_libversion(), "3.53.4") && sqlite3_threadsafe() && sqlite3_open(":memory:", &database) == SQLITE_OK;
    if (okay) okay = sqlite3_exec(database, "CREATE TABLE task(title TEXT); INSERT INTO task VALUES('phone sqlite');", NULL, NULL, NULL) == SQLITE_OK;
    if (okay) okay = sqlite3_prepare_v2(database, "SELECT title FROM task;", -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW && !strcmp((const char *)sqlite3_column_text(statement, 0), "phone sqlite");
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    if (!okay) fprintf(stderr, "embedded SQLite did not execute its persistence query\n");
    return okay ? 0 : 1;
}
