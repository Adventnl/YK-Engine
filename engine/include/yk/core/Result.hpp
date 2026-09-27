#pragma once
#include <cassert>
#include <string>
#include <utility>
#include <variant>
namespace yk {
struct Error {
    std::string message;
};
template <class T> class Result {
  public:
    Result(T value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}
    explicit operator bool() const {
        return std::holds_alternative<T>(storage_);
    }
    T &value() {
        assert(*this);
        return std::get<T>(storage_);
    }
    const T &value() const {
        assert(*this);
        return std::get<T>(storage_);
    }
    const std::string &error() const {
        assert(!*this);
        return std::get<Error>(storage_).message;
    }

  private:
    std::variant<T, Error> storage_;
};
using Status = Result<std::monostate>;
inline Status success() {
    return std::monostate{};
}
} // namespace yk
