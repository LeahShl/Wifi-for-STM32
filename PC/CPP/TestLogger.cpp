#include "TestLogger.hpp"

TestLogger::TestLogger()
{
}

TestLogger::~TestLogger()
{
}

bool TestLogger::prep()
{
    return false;
}

std::string TestLogger::strById(uint32_t id)
{
    return std::string();
}

std::string TestLogger::exportAll()
{
    return std::string();
}

uint32_t TestLogger::getNextId()
{
    return 0;
}

bool TestLogger::logTest(uint32_t id, const char *timestamp, double duration, bool success)
{
    return false;
}
