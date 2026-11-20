// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace osp {
struct Json {
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;
    std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t,
                 double, std::string, Array, Object> data;
    Json() : data(nullptr) {}
    Json(std::nullptr_t) : data(nullptr) {}
    Json(bool v) : data(v) {}
    template<class T, std::enable_if_t<std::is_integral_v<T> &&
             !std::is_same_v<T, bool>, int> = 0>
    Json(T v) : data(nullptr) {
        if constexpr (std::is_signed_v<T>) data = std::int64_t(v);
        else data = std::uint64_t(v);
    }
    Json(double v) : data(v) {}
    Json(const char* v) : data(v ? decltype(data)(std::string(v)) : decltype(data)(nullptr)) {}
    Json(std::string v) : data(std::move(v)) {}
    Json(Array v) : data(std::move(v)) {}
    static Json object(Object v) { Json j; j.data = std::move(v); return j; }
};

inline void json_string(std::ostream& out, const std::string& s) {
    constexpr char hex[] = "0123456789abcdef";
    out.put('"');
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') { out.put('\\'); out.put(char(c)); ++i; }
        else if (c < 0x20) {
            out << "\\u00" << hex[c >> 4] << hex[c & 15]; ++i;
        } else if (c < 0x80) { out.put(char(c)); ++i; }
        else {
            // Preserve valid UTF-8. Invalid individual bytes are represented as
            // U+00xx escapes so arbitrary game bytes can never invalidate JSON.
            std::size_t n = c >= 0xc2 && c <= 0xdf ? 2 :
                            c >= 0xe0 && c <= 0xef ? 3 :
                            c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            bool valid = n && i + n <= s.size();
            for (std::size_t k = 1; valid && k < n; ++k)
                valid = (static_cast<unsigned char>(s[i+k]) & 0xc0) == 0x80;
            if (valid) {
                auto d = static_cast<unsigned char>(s[i+1]);
                valid = !(c == 0xe0 && d < 0xa0) && !(c == 0xed && d >= 0xa0) &&
                        !(c == 0xf0 && d < 0x90) && !(c == 0xf4 && d >= 0x90);
            }
            if (valid) { out.write(s.data()+i, std::streamsize(n)); i += n; }
            else { out << "\\u00" << hex[c >> 4] << hex[c & 15]; ++i; }
        }
    }
    out.put('"');
}
inline void json_write(std::ostream& out, const Json& j) {
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) out << "null";
        else if constexpr (std::is_same_v<T, bool>) out << (v ? "true" : "false");
        else if constexpr (std::is_same_v<T, std::string>) json_string(out, v);
        else if constexpr (std::is_same_v<T, double>) {
            if (std::isfinite(v)) out << std::setprecision(std::numeric_limits<double>::max_digits10) << v;
            else out << "null";
        } else if constexpr (std::is_same_v<T, Json::Array>) {
            out.put('['); bool first = true;
            for (const auto& x : v) { if (!first) out.put(','); first=false; json_write(out,x); }
            out.put(']');
        } else if constexpr (std::is_same_v<T, Json::Object>) {
            out.put('{'); bool first = true;
            for (const auto& x : v) {
                if (!first) out.put(',');
                first=false;
                json_string(out,x.first); out.put(':'); json_write(out,x.second);
            }
            out.put('}');
        } else out << v;
    }, j.data);
}
inline std::string encode(const Json& j) {
    std::ostringstream out; out.imbue(std::locale::classic());
    json_write(out,j); out.put('\n'); return out.str();
}
inline std::string hex(std::uint64_t n, unsigned width = 8) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::uppercase << std::hex << std::setfill('0') << std::setw(int(width)) << n;
    return s.str();
}
}
