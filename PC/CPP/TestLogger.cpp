#include "TestLogger.hpp"
#include <iostream>
#include <filesystem>
#include <sstream>

/**
 * @brief Construct a new Test Logger:: Test Logger object
 * 
 */
TestLogger::TestLogger()
{
    const char *home = std::getenv("HOME");
    if (!home)
    {
        throw std::runtime_error("HOME environment variable not set");
    }
    std::filesystem::path base_dir = std::filesystem::path(home) / "HW_tester";
    std::filesystem::create_directories(base_dir);
    std::filesystem::path tmp_path = base_dir / "records.db";
    db_path = tmp_path.string();
}

/**
 * @brief Destroy the Test Logger:: Test Logger object
 * 
 */
TestLogger::~TestLogger() = default;

/**
 * @brief Prep database for operations
 * 
 * @throw std::runtime_error
 */
void TestLogger::prep()
{
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3 *db = nullptr;

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        throw std::runtime_error("prep: Cannot open DB");
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS test_logs ("
        "test_id INTEGER, "
        "timestamp TEXT, "
        "duration REAL, "
        "result INTEGER);";

    char *err_msg = nullptr;
    if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK)
    {
        std::string error = err_msg ? err_msg : "unknown";
        sqlite3_free(err_msg);
        sqlite3_close(db);
        throw std::runtime_error("prep: Create table error: " + error);
    }

    sqlite3_close(db);
}

/**
 * @brief Get a string representation of test results by ID
 * 
 * @param id Test ID
 * @return std::string Test results in a string format
 * @throw std::runtime_error
 */
std::string TestLogger::strById(uint32_t id)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        throw std::runtime_error("strById: Cannot open DB");
    }

    const char *query = "SELECT test_id, timestamp, duration, result "
                        "FROM test_logs WHERE test_id = ?;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        throw std::runtime_error("strById: Failed to prepare statement");
    }

    sqlite3_bind_int(stmt, 1, id);

    std::ostringstream data;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *timestamp = sqlite3_column_text(stmt, 1);
        double duration = sqlite3_column_double(stmt, 2);
        int result = sqlite3_column_int(stmt, 3);

        data << "Test ID: " << id << "\n"
             << "Start Time: " << timestamp << "\n"
             << "Duration: " << duration << " seconds\n"
             << "Result: " << (result? "Success" : "Failure");
    }
    else
    {
        data << "No test record found for this ID";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return data.str();
}

/**
 * @brief Get a CSV-formatted string of the test data
 * 
 * @return std::string Test data
 * @throw std::runtime_error
 */
std::string TestLogger::exportAll()
{
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        throw std::runtime_error("exportAll: Cannot open DB");
    }

    const char *query = "SELECT test_id, timestamp, duration, result "
                        "FROM test_logs ORDER BY test_id ASC;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        throw std::runtime_error("exportAll: Failed to prepare statement");
    }

    std::ostringstream data;
    data << "test_id, timestamp, duration, result\n";

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *timestamp = sqlite3_column_text(stmt, 1);
        double duration = sqlite3_column_double(stmt, 2);
        int result = sqlite3_column_int(stmt, 3);

        data << id << "," << timestamp << "," << duration << "," << result << "\n";
    }

    if (rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        throw std::runtime_error("exportAll: Error while reading rows");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return data.str();
}

/**
 * @brief Get next test ID
 * 
 * @return uint32_t Next test ID
 * @throw std::runtime_error
 */
uint32_t TestLogger::getNextId()
{
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        throw std::runtime_error("getNextId: Cannot open DB");
    }

    const char* query = "SELECT MAX(test_id) FROM test_logs;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        throw std::runtime_error("getNextId: Failed to prepare statement");
    }

    int rc = sqlite3_step(stmt);
    uint32_t next_id = 1;
    if (rc == SQLITE_ROW)
    {
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        {
            next_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 0) + 1);
        }
    }
    else
    {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        throw std::runtime_error("getNextId: Step failed");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return next_id;
}

/**
 * @brief Log results of a test
 * 
 * @param test_id Unique test ID (use get_next_id() to get it beforehand)
 * @param timestamp Timestamp string in ISO 8601 format
 * @param duration_sec Test duration in seconds
 * @param result Test result
 * @throw std::runtime_error
 */
void TestLogger::logTest(uint32_t test_id, const char *timestamp, double duration_sec, bool result)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        throw std::runtime_error("logTest: Cannot open DB");
    }

    const char *query = "INSERT INTO test_logs (test_id, timestamp, duration, result) "
                        "VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        throw std::runtime_error("logTest: Failed to prepare statement");
    }

    sqlite3_bind_int(stmt, 1, test_id);
    sqlite3_bind_text(stmt, 2, timestamp, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, duration_sec);
    sqlite3_bind_int(stmt, 4, result);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        throw std::runtime_error("logTest: Insert step error");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}