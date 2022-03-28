#ifndef DEFI_MASTERNODES_RES_H
#define DEFI_MASTERNODES_RES_H

#include <optional>
#include <string>
#include <tinyformat.h>
#include <tuple>

struct Res
{
    bool ok;
    std::string msg;
    uint32_t code;
    std::string dbgMsg; // CValidationState support

    Res() = delete;

    operator bool() const {
        return ok;
    }

    template<typename... Args>
    static Res Err(std::string const & err, const Args&... args) {
        return Res{false, tfm::format(err, args...), 0, {} };
    }

    template<typename... Args>
    static Res ErrCode(uint32_t code, std::string const & err, const Args&... args) {
        return Res{false, tfm::format(err, args...), code, {} };
    }

    // extended version just for CValidationState support
    template<typename... Args>
    static Res ErrDbg(std::string const & debugMsg, std::string const & err, const Args&... args) {
        return {false, tfm::format(err, args...), 0, debugMsg };
    }

    template<typename... Args>
    static Res Ok(std::string const & msg, const Args&... args) {
        return Res{true, tfm::format(msg, args...), 0, {} };
    }

    static Res Ok() {
        return Res{true, {}, 0, {} };
    }
};

template <typename T>
struct ResVal : public Res
{
    std::optional<T> val{};

    ResVal() = delete;

    ResVal(Res const & errRes) : Res(errRes) {
        assert(!this->ok); // if value is not provided, then it's always an error
    }
    ResVal(T value, Res const & okRes) : Res(okRes), val(std::move(value)) {
        assert(this->ok); // if value if provided, then it's never an error
    }

    operator bool() const {
        return ok;
    }

    operator T() const {
        assert(ok);
        return *val;
    }

    const T& operator*() const {
        assert(ok);
        return *val;
    }

    const T* operator->() const {
        assert(ok);
        return &(*val);
    }

    T& operator*() {
        assert(ok);
        return *val;
    }

    template <typename F>
    T ValOrException(F&& func) const {
        if (!ok) {
            throw func(code, msg);
        }
        return *val;
    }

    T ValOrDefault(T default_) const {
        if (!ok) {
            return std::move(default_);
        }
        return *val;
    }
};

template<typename T>
Res ResOrErr(T&& res) {
    if constexpr (std::is_convertible_v<T, Res>) {
        return std::forward<T>(res);
    } else {
        return Res{false};
    }
}

template<typename...Args>
Res StrToRes(Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        return Res::Ok();
    } else if constexpr (std::is_convertible_v<std::tuple_element_t<0, std::tuple<Args...>>, uint32_t>) {
        return Res::ErrCode(args...);
    } else {
        return Res::Err(args...);
    }
}

#define verifyRes(x, ...) do if (auto res = x; !res) { auto tmp = ::StrToRes(__VA_ARGS__); return tmp ? ::ResOrErr(std::move(res)) : tmp; } while(0)
#define verifyDecl(x, y, ...) auto x = y; do if (!x) { auto tmp = ::StrToRes(__VA_ARGS__); return tmp ? ::ResOrErr(std::move(x)) : tmp; } while(0)

#endif //DEFI_MASTERNODES_RES_H
