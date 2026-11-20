// SPDX-License-Identifier: MIT OR Apache-2.0
#include "osp/console.hpp"
#include <cassert>
int main() {
    assert(osp::snapshot_tag("run:case-1_a.2\r\n").value()=="run:case-1_a.2");
    assert(osp::snapshot_tag("").value().empty());
    assert(!osp::snapshot_tag("bad tag"));
    assert(!osp::snapshot_tag("bad\nnext"));
    assert(!osp::snapshot_tag(std::string(161,'x')));
    assert(osp::snapshot_control("")->tag.empty());
    assert(!osp::snapshot_control("tag")->light);
    assert(osp::snapshot_control("tag\r\nmode=light\r\n")->light);
    assert(!osp::snapshot_control("tag\nmode=full\n")->light);
    for (auto text:{"tag \nmode=light", "tag\nmode=LIGHT", "tag\nmode=light\nextra",
                    "tag\nmode=light ","tag\n", "tag\nmode=light\n\n", "tag\nmode=light\x01", "tag\r", "tag\rmode=light"}) {
        // A single terminal newline is permitted; the one-line tag case is too.
        if (std::string(text)=="tag\n") assert(osp::snapshot_control(text));
        else assert(!osp::snapshot_control(text));
    }
    assert(!osp::snapshot_control(std::string(193,'x')));
    assert(!osp::snapshot_control(std::string("tag\0bad",7)));
    assert(!osp::snapshot_control("t\xc3\xa1g\nmode=light"));
    auto omitted=osp::encode(osp::omitted_section());
    assert(omitted.find("omitted_by_request")!=std::string::npos);
    assert(omitted.find("\"complete\":null")!=std::string::npos);
    osp::layout::ConsoleCommand original{};
    original.opcode=0x123; original.parse=0x456; original.parameters=0x999;
    auto result=osp::snapshot_command(original,1,2,3,4);
    assert(result.opcode==original.opcode && result.parse==original.parse);
    assert(result.long_name==1 && result.short_name==2 && result.help==3 && result.execute==4);
    assert(result.parameter_count==0 && result.parameters==0 && result.needs_parent==0 && result.flags==0);
}
