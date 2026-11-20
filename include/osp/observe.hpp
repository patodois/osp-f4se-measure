// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include "osp/json.hpp"
#include "osp/layouts.hpp"
#include <cstring>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <array>

namespace osp {
inline bool address_range(layout::Address p, std::size_t n) {
    return p != 0 && n != 0 && n-1 <= UINT64_MAX-p;
}
inline bool valid_array(const layout::Array& a) {
    return a.count <= a.capacity && a.capacity <= 1000000 &&
           (!a.count || address_range(a.entries, std::size_t(a.count)*8));
}
// Reader provides bool read(Address, void*, size_t). Reads never dereference
// game pointers in plugin code; Windows uses ReadProcessMemory on itself.
template<class Reader, class T>
std::optional<T> read_at(Reader& r, layout::Address p) {
    T v{};
    if (!address_range(p,sizeof(v)) || !r.read(p,&v,sizeof(v))) return std::nullopt;
    return v;
}
template<class Reader>
std::optional<std::string> read_string(Reader& r, layout::Address p, std::size_t limit=65536) {
    if (!p) return std::nullopt;
    std::string s;
    // Bulk reads keep localized text inexpensive. If a chunk crosses an
    // inaccessible page, fall back to bytes so its preceding terminator works.
    for (std::size_t i=0; i<limit;) {
        if (i > UINT64_MAX-p) return std::nullopt;
        std::array<char,256> block{};
        auto n=std::min(block.size(),limit-i);
        if (address_range(p+i,n) && r.read(p+i,block.data(),n)) {
            auto end=std::find(block.begin(),block.begin()+n,'\0');
            s.append(block.begin(),end);
            if (end!=block.begin()+n) return s;
            i+=n;
        } else {
            auto c=read_at<Reader,char>(r,p+i);
            if (!c) return std::nullopt;
            if (!*c) return s;
            s.push_back(*c); ++i;
        }
    }
    return std::nullopt;
}
inline std::string packed_version(std::uint32_t v) {
    return std::to_string(v>>24) + "." + std::to_string((v>>16)&255) + "." +
           std::to_string((v>>4)&4095) + "." + std::to_string(v&15);
}
inline bool compatible(std::uint32_t runtime, std::uint32_t f4se,
                       std::uint32_t editor, std::uint32_t release) {
    return !editor && runtime==0x010a0a30 && f4se==0x00060170 && release==25;
}
enum class Trigger { none, loaded, new_game, console };
inline Trigger trigger(std::uint32_t type, std::uint64_t data, std::uint32_t length) {
    if (type==3 && length==1 && data==1) return Trigger::loaded;
    if (type==8) return Trigger::new_game;
    return Trigger::none;
}
// Decoding rules published by Setting::GetType/GetDouble in GameSettings.cpp.
// Used only with an independently obtained Setting; not an enumeration method.
inline Json decode_setting(const std::string& name, std::uint64_t bits,
                           const std::optional<std::string>& text) {
    std::string type="unknown"; Json value;
    if (!name.empty()) switch (name[0]) {
        case 'b': type="bool"; value=bool(bits&255); break;
        case 'i': { type="integer"; std::int32_t n; auto lo=std::uint32_t(bits);
                    std::memcpy(&n,&lo,4); value=n; break; }
        case 'f': { type="float"; float n; auto lo=std::uint32_t(bits);
                    std::memcpy(&n,&lo,4); value=double(n); break; }
        case 's': case 'S': type="string"; if (text) value=*text; break;
        case 'r': type="id6"; value=std::uint32_t(bits)>>8; break;
        case 'a': type="id"; value=std::uint32_t(bits); break;
        default: break;
    }
    return Json::object({{"name",name},{"type",type},{"value",value},{"raw_bits_hex",hex(bits,16)}});
}
}
