// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include "osp/settings.hpp"
namespace osp {
inline std::optional<std::string> snapshot_tag(std::string text) {
    auto end=text.find_last_not_of(" \t\r\n");
    if (end==std::string::npos) return std::string();
    text.resize(end+1);
    if (text.size()>160) return std::nullopt;
    for (unsigned char c:text)
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
              c=='_'||c=='-'||c=='.'||c==':')) return std::nullopt;
    return text;
}
struct CaptureRequest { std::string tag; bool light=false; };
inline std::optional<CaptureRequest> snapshot_control(std::string text) {
    if (text.size()>192) return std::nullopt;
    for (std::size_t i=0;i<text.size();++i)
        if (text[i]=='\r' && (i+1==text.size() || text[i+1]!='\n')) return std::nullopt;
    if (!text.empty() && text.back()=='\n') text.pop_back();
    std::vector<std::string> lines;
    std::size_t begin=0;
    do {
        auto end=text.find('\n',begin);
        auto line=text.substr(begin,end==std::string::npos?end:end-begin);
        if (!line.empty() && line.back()=='\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end==std::string::npos) break;
        begin=end+1;
    } while (true);
    if (lines.size()>2) return std::nullopt;
    auto tag=snapshot_tag(lines[0]);
    if (!tag || *tag!=lines[0]) return std::nullopt;
    if (lines.size()==2 && lines[1]!="mode=light" && lines[1]!="mode=full") return std::nullopt;
    return CaptureRequest{*tag,lines.size()==2 && lines[1]=="mode=light"};
}
inline Json omitted_section() {
    return Json::object({{"status","omitted_by_request"},{"complete",nullptr},
        {"values_complete",nullptr},{"requested",false}});
}
inline layout::ConsoleCommand snapshot_command(layout::ConsoleCommand original,
        layout::Address name,layout::Address empty,layout::Address help,layout::Address execute) {
    original.long_name=name; original.short_name=empty; original.help=help;
    original.needs_parent=0; original.parameter_count=0; original.parameters=0;
    original.execute=execute; original.evaluate=0; original.flags=0;
    return original;
}
}
