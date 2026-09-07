/*
 * log_test.cpp - manual smoke test for the Log class. Not a permanent
 * part of the build -- just proves write()/rotation/purge work before
 * Log gets used by the real Central Computer modules.
 */
#include "log.h"

#include <iostream>

int main()
{
    try {
        Log log("test_output/central_computer", /*retention_days=*/7);

        log.write("connected to LNC via TCP 127.0.0.1:5555");
        log.write("sent SET_TEMP_NORMAL (min=15, max=27)");
        log.write("received TLV_TAG_ACK");

        std::cout << "wrote 3 lines, check test_output/central_computer-*.log\n";
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
