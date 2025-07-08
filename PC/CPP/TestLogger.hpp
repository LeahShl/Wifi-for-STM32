#pragma once

#include <string>

class TestLogger
{
public:
    TestLogger();
    ~TestLogger();

    bool prep();
    std::string strById(uint32_t id);
    std::string exportAll();
    uint32_t getNextId();
    bool logTest(uint32_t id, const char* timestamp, double duration, bool success);
};