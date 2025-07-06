#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "tests_db.h"

static char *db_path;

/****************************
 * FUNCTION IMPLEMENTATION  *
 ****************************/

int init_db (void)
{
#ifdef LOCAL_DB_PATH
    db_path = "test_records.db";
#else
#include <sys/stat.h>
	// construct path
    static char buf[512];
    const char *home = getenv("HOME");
    if (!home) {
        perror("HOME environment variable not set\n");
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s/HW_tester/records.db", home);

    // ensure existance of directory
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/HW_tester", home);
    mkdir(dir_path, 0755); 
    
    // init path pointer
    db_path = buf;
#endif

    // create the database
    sqlite3 *db;
    char *err_msg = NULL;

    if (sqlite3_open(db_path, &db) != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS test_logs ("
        "test_id INTEGER, "
        "timestamp TEXT, "
        "duration REAL, "
        "result INTEGER);";

    if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }

    sqlite3_close(db);

	return 1;
}

int log_test (uint32_t test_id, const char *timestamp,
             double duration_sec, int result)
{
	sqlite3 *db;

    // open db
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    // prepare statement
    sqlite3_stmt *stmt;
    const char *sql_insert = "INSERT INTO test_logs (test_id, timestamp, duration, result) "
                             "VALUES (?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Insert prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    // bind variables
    sqlite3_bind_int(stmt, 1, test_id);
    sqlite3_bind_text(stmt, 2, timestamp, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, duration_sec);
    sqlite3_bind_int(stmt, 4, result); 

    // insert log
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "Insert step error: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
}

int print_all_logs (void)
{
	sqlite3 *db;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT test_id, timestamp, duration, result "
                      "FROM test_logs ORDER BY test_id ASC;";

    // open db
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    // prepare statement
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    // csv header
    printf("test_id, timestamp, duration, result\n");

    // csv data
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *timestamp = sqlite3_column_text(stmt, 1);
        double duration = sqlite3_column_double(stmt, 2);
        int result = sqlite3_column_int(stmt, 3);

        printf("%d,%s,%f,%d\n", id, timestamp, duration, result);
    }

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "Error while reading rows: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
}

int print_log_by_id (uint32_t test_id)
{
	sqlite3 *db;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT test_id, timestamp, duration, result "
                      "FROM test_logs WHERE test_id = ?;";

    // open db
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    // prepare statement
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    // bind variable
    sqlite3_bind_int(stmt, 1, test_id);

    // print record
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *timestamp = sqlite3_column_text(stmt, 1);
        double duration = sqlite3_column_double(stmt, 2);
        int result = sqlite3_column_int(stmt, 3);

        printf("Test ID: %d\n", id);
        printf("Start Time: %s\n", timestamp);
        printf("Duration: %.6f seconds\n", duration);
        printf("Result: %s\n", result? "Success" : "Failure");
    }
    else
    {
        printf("No record found for test ID %u.\n", test_id);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
}

int get_next_id (uint32_t *dest)
{
	sqlite3 *db;
    sqlite3_stmt *res;

    // open db
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
		fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    // prepare statement
    const char *sql = "SELECT MAX(test_id) FROM test_logs;";
    rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);
    if (rc != SQLITE_OK)
    {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    // find next ID
    rc = sqlite3_step(res);
    if (rc == SQLITE_ROW && sqlite3_column_type(res, 0) != SQLITE_NULL)
    {
        *dest = sqlite3_column_int(res, 0) + 1;
    }
    else
    {
        *dest = 0; // no records yet
    }

    sqlite3_finalize(res);
    sqlite3_close(db);
    return 1;
}
