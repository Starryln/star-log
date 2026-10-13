#include "star_log.hpp"

int main()
{
    star_log::SetLogToFile("test.log");
    LOG(INFO)  << "INFO Test";
    LOG(DEBUG) << "DEBUG Test";
    LOG(WARN)  << "WARN Test";
    LOG(ERROR) << "ERROR Test";
}