#pragma once

#include <string>
#include <sqlite3.h>
#include <mutex>

class TestLogger
{
public:
    TestLogger();
    ~TestLogger();

    void prep();
    std::string strById(uint32_t id);
    std::string exportAll();
    uint32_t getNextId();
    void logTest(uint32_t test_id, const char *timestamp, double duration_sec, bool result);

private:
    std::string db_path;
    std::mutex db_mutex;
};