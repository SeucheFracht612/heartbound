#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace heartstead::core {

struct Error {
    std::string code;
    std::string message;
};

class Status {
  public:
    static Status ok() {
        return Status();
    }

    static Status failure(std::string code, std::string message) {
        return Status(Error{std::move(code), std::move(message)});
    }

    [[nodiscard]] bool is_ok() const noexcept {
        return !error_.has_value();
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    [[nodiscard]] const Error& error() const {
        if (!error_.has_value()) {
            throw std::logic_error("Status::error() called on ok status");
        }
        return *error_;
    }

  private:
    Status() = default;

    explicit Status(Error error) : error_(std::move(error)) {}

    std::optional<Error> error_;
};

template <typename T> class Result {
  public:
    static Result success(T value) {
        return Result(std::move(value));
    }

    static Result failure(std::string code, std::string message) {
        return Result(Error{std::move(code), std::move(message)});
    }

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(state_);
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] T& value() & {
        if (!has_value()) {
            throw std::logic_error("Result::value() called on failed result");
        }
        return std::get<T>(state_);
    }

    [[nodiscard]] const T& value() const& {
        if (!has_value()) {
            throw std::logic_error("Result::value() called on failed result");
        }
        return std::get<T>(state_);
    }

    [[nodiscard]] T&& value() && {
        if (!has_value()) {
            throw std::logic_error("Result::value() called on failed result");
        }
        return std::move(std::get<T>(state_));
    }

    [[nodiscard]] const Error& error() const {
        if (has_value()) {
            throw std::logic_error("Result::error() called on successful result");
        }
        return std::get<Error>(state_);
    }

  private:
    explicit Result(T value) : state_(std::move(value)) {}

    explicit Result(Error error) : state_(std::move(error)) {}

    std::variant<T, Error> state_;
};

} // namespace heartstead::core
