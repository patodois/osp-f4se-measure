// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include "osp/observe.hpp"
#include <set>

namespace osp {
constexpr std::size_t max_names_bytes=4*1024*1024, max_setting_names=100000;
struct SettingsNames {
    std::vector<std::string> names, errors;
    std::size_t duplicates=0;
};
inline SettingsNames parse_settings_names(std::string bytes) {
    SettingsNames out;
    if (bytes.size()>max_names_bytes) { out.errors.push_back("settings list exceeds 4 MiB"); return out; }
    if (bytes.compare(0,3,"\xef\xbb\xbf")==0) bytes.erase(0,3);
    std::istringstream input(bytes); std::string line; std::set<std::string> seen;
    std::size_t number=0;
    while (std::getline(input,line)) {
        ++number;
        auto first=line.find_first_not_of(" \t\r");
        if (first==std::string::npos || line[first]=='#') continue;
        auto last=line.find_last_not_of(" \t\r");
        line=line.substr(first,last-first+1);
        bool valid=line.size()<=1024;
        for (unsigned char c:line) valid=valid && c>=0x21 && c<=0x7e;
        if (!valid) { out.errors.push_back("invalid settings name at line "+std::to_string(number)); continue; }
        if (!seen.insert(line).second) { ++out.duplicates; continue; }
        if (out.names.size()==max_setting_names) { out.errors.push_back("too many settings names"); break; }
        out.names.push_back(line);
    }
    if (out.names.empty()) out.errors.push_back("settings list contains no names");
    return out;
}
inline bool settings_coverage_complete(const SettingsNames& names,std::size_t queried) {
    return names.errors.empty() && !names.names.empty() && queried==names.names.size();
}
struct SettingLookup {
    bool queried=false;
    layout::Address setting=0;
    std::vector<std::string> errors;
};
// Calls implements guarded construct/find/release operations. The only map
// operation is the SDK-published function; no map fields are examined.
template<class Calls>
SettingLookup lookup_setting(Calls& calls,layout::Address map,const std::string& name) {
    SettingLookup result;
    if (!map) { result.errors.push_back("null collection map"); return result; }
    layout::FixedString key{};
    if (!calls.construct(&key,name.c_str())) {
        result.errors.push_back("fixed-string constructor fault"); return result;
    }
    // The constructor succeeded: always balance its reference, even if find
    // fails. Do not allocate/throw between acquiring and releasing the key.
    layout::Address slot=0;
    auto cursor=&slot;
    bool has_key=key.entry!=0;
    bool ok=has_key && calls.find(map,&key,&cursor);
    bool released=calls.release(&key);
    bool contract_ok=cursor==&slot;
    result.queried=ok && contract_ok;
    if (result.queried) result.setting=slot;
    if (!has_key) result.errors.push_back("fixed-string constructor returned a null entry");
    else if (!ok) result.errors.push_back("GetSetting call fault");
    else if (!contract_ok) result.errors.push_back("GetSetting output differs from the published SDK wrapper contract");
    if (!released) result.errors.push_back("fixed-string release fault");
    return result;
}
inline Json* field(Json& object,const std::string& name) {
    auto pairs=std::get_if<Json::Object>(&object.data);
    if (pairs) for (auto& p:*pairs) if (p.first==name) return &p.second;
    return nullptr;
}
inline bool flag(const Json& object,const std::string& name) {
    auto pairs=std::get_if<Json::Object>(&object.data);
    if (pairs) for (const auto& p:*pairs) if (p.first==name) {
        auto b=std::get_if<bool>(&p.second.data); return b && *b;
    }
    return false;
}
struct SettingObservation {
    Json json;
    bool queried=false, found=false, value_read=false;
    std::vector<std::string> errors;
};
template<class Reader>
SettingObservation observe_setting(Reader& reader,const std::string& name,SettingLookup lookup) {
    SettingObservation out;
    out.queried=lookup.queried; out.found=lookup.queried && lookup.setting;
    out.errors=std::move(lookup.errors);
    Json type,value,registered,prefix,raw,raw32,encoding;
    if (out.found) {
        auto setting=read_at<Reader,layout::Setting>(reader,lookup.setting);
        auto actual=setting ? read_string(reader,setting->name,1025) : std::nullopt;
        if (!setting) out.errors.push_back("unreadable Setting");
        else {
            raw=hex(setting->bits,16); raw32=std::uint32_t(setting->bits);
            if (!actual || actual->empty()) out.errors.push_back("unreadable or empty registered name");
            else {
                registered=*actual; prefix=actual->substr(0,1);
                std::optional<std::string> text;
                if ((*actual)[0]=='s' || (*actual)[0]=='S') text=read_string(reader,setting->bits);
                auto decoded=decode_setting(*actual,setting->bits,text);
                type=*field(decoded,"type"); value=*field(decoded,"value");
                auto decoded_type=std::get<std::string>(type.data);
                encoding="decoded";
                out.value_read=true;
                if (decoded_type=="string" && !text) {
                    out.value_read=false; out.errors.push_back("null, unreadable or oversized string value");
                } else if (decoded_type=="float" && !std::isfinite(std::get<double>(value.data))) {
                    value=nullptr; out.value_read=false; out.errors.push_back("non-finite float value");
                } else if (decoded_type=="unknown") {
                    // GetType explicitly marks 'u' unknown. Expose the published
                    // union's raw u32 rather than invent a semantic type for it.
                    encoding="raw_u32"; value=std::uint32_t(setting->bits);
                    out.errors.push_back("SDK type unknown; value is raw Data.u32");
                }
            }
        }
    } else if (out.queried) out.errors.push_back("not found");
    Json::Array errors; for (const auto& e:out.errors) errors.emplace_back(e);
    out.json=Json::object({{"name",name},{"lookup_completed",out.queried},
        {"found",out.queried?Json(out.found):Json()},{"registered_name",registered},
        {"type",type},{"type_prefix",prefix},{"value",value},{"value_encoding",encoding},
        {"raw_bits_hex",raw},{"raw_u32",raw32},{"errors",errors}});
    return out;
}
}
