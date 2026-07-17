#include "engine/core/process_entry.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    using heartstead::core::run_process_entry;

    assert(run_process_entry("process-entry-test", [] { return 7; }) == 7);
    assert(run_process_entry("process-entry-test", []() -> int {
               throw std::runtime_error("expected test exception");
           }) == EXIT_FAILURE);
    assert(run_process_entry("process-entry-test", []() -> int { throw 42; }) == EXIT_FAILURE);

    std::cout.setstate(std::ios::badbit);
    assert(run_process_entry("process-entry-test", [] { return 0; }) == EXIT_FAILURE);
    std::cout.clear();
    return 0;
}
