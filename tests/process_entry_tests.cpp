#include "engine/core/process_entry.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    using heartstead::core::run_no_argument_process_entry;
    using heartstead::core::run_process_entry;

    assert(run_process_entry("process-entry-test", [] { return 7; }) == 7);
    assert(run_process_entry("process-entry-test", []() -> int {
               throw std::runtime_error("expected test exception");
           }) == EXIT_FAILURE);
    assert(run_process_entry("process-entry-test", []() -> int { throw 42; }) == EXIT_FAILURE);

    char executable[] = "sample";
    char help[] = "--help";
    char unexpected[] = "unexpected";
    char* no_arguments[]{executable, nullptr};
    char* help_arguments[]{executable, help, nullptr};
    char* unexpected_arguments[]{executable, unexpected, nullptr};
    assert(run_no_argument_process_entry(1, no_arguments, [] { return 9; }) == 9);
    assert(run_no_argument_process_entry(2, help_arguments, [] { return 9; }) == 0);
    assert(run_no_argument_process_entry(2, unexpected_arguments, [] { return 9; }) == 2);
    assert(run_no_argument_process_entry(2, nullptr, [] { return 9; }) == 2);

    std::cout.setstate(std::ios::badbit);
    assert(run_process_entry("process-entry-test", [] { return 0; }) == EXIT_FAILURE);
    std::cout.clear();
    return 0;
}
