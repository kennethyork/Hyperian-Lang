#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static int create_legacy(const char *path) {
    remove(path); sqlite3 *database = NULL;
    if (sqlite3_open(path, &database) != SQLITE_OK) return 1;
    const char *sql =
        "CREATE TABLE hyperian_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE hyperian_records (model TEXT NOT NULL, record_id TEXT NOT NULL, field_name TEXT NOT NULL, field_value TEXT NOT NULL, field_order INTEGER NOT NULL, PRIMARY KEY(model,record_id,field_name));"
        "INSERT INTO hyperian_meta VALUES('data_version','1');"
        "INSERT INTO hyperian_records VALUES('Task','1','id','1',0);"
        "INSERT INTO hyperian_records VALUES('Task','1','title','Keep this SQLite value',1);";
    int okay = sqlite3_exec(database, sql, NULL, NULL, NULL) == SQLITE_OK; sqlite3_close(database); return !okay;
}

static int verify_migrated(const char *path) {
    sqlite3 *database = NULL; sqlite3_stmt *statement = NULL; int okay = sqlite3_open(path, &database) == SQLITE_OK;
    if (okay) okay = sqlite3_prepare_v2(database,
        "SELECT (SELECT value FROM hyperian_meta WHERE key='data_version'),"
        "(SELECT field_value FROM hyperian_records WHERE model='Task' AND record_id='1' AND field_name='name'),"
        "(SELECT count(*) FROM hyperian_records WHERE field_name='title');", -1, &statement, NULL) == SQLITE_OK;
    if (okay) okay = sqlite3_step(statement) == SQLITE_ROW && !strcmp((const char *)sqlite3_column_text(statement, 0), "2") &&
        !strcmp((const char *)sqlite3_column_text(statement, 1), "Keep this SQLite value") && sqlite3_column_int(statement, 2) == 0;
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return !okay;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    if (!strcmp(argv[1], "create")) return create_legacy(argv[2]);
    if (!strcmp(argv[1], "verify")) return verify_migrated(argv[2]);
    return 2;
}
