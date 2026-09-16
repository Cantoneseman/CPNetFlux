#pragma once

#include <string>
#include <utility>

namespace cpnetflux::common {

enum class StatusCode {
    Ok,
    InvalidArgument,
    SystemError,
    RuntimeError,
};

class Status {
   public:
    Status() = default;

    static Status ok() { return {}; }

    static Status invalidArgument(std::string message) {
        return Status(StatusCode::InvalidArgument, std::move(message), 0);
    }

    static Status systemError(std::string message, int errorNumber) {
        return Status(StatusCode::SystemError, std::move(message), errorNumber);
    }

    static Status runtimeError(std::string message) {
        return Status(StatusCode::RuntimeError, std::move(message), 0);
    }

    [[nodiscard]] bool isOk() const noexcept { return code_ == StatusCode::Ok; }

    [[nodiscard]] StatusCode code() const noexcept { return code_; }

    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    [[nodiscard]] int errorNumber() const noexcept { return errorNumber_; }

   private:
    Status(StatusCode code, std::string message, int errorNumber)
        : code_(code), message_(std::move(message)), errorNumber_(errorNumber) {}

    StatusCode code_ = StatusCode::Ok;
    std::string message_;
    int errorNumber_ = 0;
};

template <typename T>
class Result {
   public:
    Result(T value) : value_(std::move(value)), status_(Status::ok()), hasValue_(true) {}
    Result(Status status) : status_(std::move(status)), hasValue_(false) {}

    [[nodiscard]] bool isOk() const noexcept { return hasValue_; }

    [[nodiscard]] const T& value() const noexcept { return value_; }

    [[nodiscard]] T& value() noexcept { return value_; }

    [[nodiscard]] const Status& status() const noexcept { return status_; }

   private:
    T value_{};
    Status status_;
    bool hasValue_ = false;
};

}  // namespace cpnetflux::common
