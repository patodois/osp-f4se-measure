// SPDX-License-Identifier: MIT OR Apache-2.0
// Native tests of the v9 sequencer logic: no Windows API, no game, no files
// except those named on the command line by tests/test_sequencer.py.
#include "osp/sequencer.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace osp::seq;
static int failures=0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); ++failures; } } while (0)

static const std::string cell="OSPBenchCell02";
static const char* bats[][5]={
    {"t00a","start","-","ospsnap\r\n",nullptr},{"t00b","note","-","ospsnap\r\ncoc OSPBenchCell02\r\n",nullptr},
    {"t00c","cell","-","tcl\r\nospsnap\r\n",nullptr}};
struct Built { std::string text; std::vector<std::pair<std::string,std::string>> files; };
static const std::string exe_sha=std::string(64,'e');
// Two steps at distinct poses, photo_v2 structure, bench index from the plan.
static Built build_plan(const std::string& packet_sha,const char* tolerance="3C23D70A") {
    Built b; std::ostringstream o;
    o<<"osp-plan 2\npacket test.packet 1 "<<packet_sha<<"\nbench OSP|Bench.esp 10 "<<std::string(64,'b')<<"\ncell "<<cell<<" 001000\nscreenshot ScreenShot\n";
    o<<"exe 65503104 "<<exe_sha<<"\n";
    o<<"plugin full Fallout4.esm\nplugin full OSP|Bench.esp\nini iSize|W:Display\n";
    auto part=[&](const std::string& name,const std::string& role,const std::string& step,int shots,const std::string& body,const std::string& extra){
        b.files.push_back({name,body});
        o<<"part "<<name<<" "<<osp::Sha256::of(body)<<" "<<role<<" "<<step<<" "<<shots<<extra<<"\n";
    };
    for (auto& r:bats) part(r[0],r[1],r[2],0,r[3],"");
    const char* poses[2][3]={{"46000000","461F6000","C2E29ECD"},{"46200000","4620A000","C2E29ECD"}};
    for (int s=0;s<2;++s) {
        std::string tag="t0"+std::to_string(s+1), step="step"+std::to_string(s+1);
        part(tag+"a","pose",step,0,"player.setpos x 8192\r\nplayer.setangle z 0\r\nospsnap\r\n","");
        part(tag+"b","settle",step,0,"ospsnap\r\n",std::string(" 40 require ")+poses[s][0]+" "+poses[s][1]+" "+poses[s][2]+" "+tolerance);
        part(tag+"c","free",step,0,"tfc 1\r\nospsnap\r\n","");
        part(tag+"d","capture",step,0,"tm\r\nospsnap\r\n"," 3 1 001034");
        part(tag+"e","photo",step,1,"Screenshot\r\nospsnap\r\n","");
        part(tag+"f","repeat",step,1,"Screenshot\r\nospsnap\r\n","");
        part(tag+"g","restore",step,0,"tm\r\ntfc 1\r\nospsnap\r\n","");
    }
    part("t99","end","-",0,"tcl\r\nospsnap\r\n","");
    o<<"end "<<b.files.size()<<"\n";
    b.text=o.str(); return b;
}
static Plan parsed(const std::string& text) { std::string e; auto p=parse_plan(text,e); if (!p) std::fprintf(stderr,"plan: %s\n",e.c_str()); return *p; }
static std::uint32_t f32(float v) { std::uint32_t w; std::memcpy(&w,&v,4); return w; }

static void sha256_vectors() {
    CHECK(osp::Sha256::of("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(osp::Sha256::of("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(osp::Sha256::of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    osp::Sha256 h; std::string a(1000,'a'); for (int i=0;i<1000;++i) h.update(a.data(),a.size());
    CHECK(h.hex()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
static void closed_list() {
    for (auto s:{"player.setpos x 8192","player.setpos z -113.310157776","Player.SetPos y 1.5","player.setangle z 180",
                 "player.setangle x -360","tfc","tfc 1","tm","TCL","Screenshot","coc OSPBenchCell02","ospsnap"})
        CHECK(check_command(s,cell).ok);
    for (auto s:{"save quick","qqq","player.setav health 1","player.setpos x 1e3","player.setpos w 1","player.setangle z 1.5",
                 "player.setangle z 361","tfc 2","tm 1","coc Sanctuary","tcl;save x","bat other","ospsnap tag"," tm","tm ",
                 "player.setpos  x 1","prid 14","ospnodes","screenshot \"a\"","player.setpos x 12345678","player.setpos x 1.1234567891",""})
        CHECK(!check_command(s,cell).ok);
    CHECK(!check_command("coc OSPBenchCell02","").ok);
    CHECK(check_bat("Screenshot\r\nospsnap\r\n",cell).shots==1);
    CHECK(check_bat("tm\r\ntfc 1\r\nospsnap\r\n",cell).ok);
    // Round 25: the picture is taken after the file has run, so no toggle may share its file, in either order.
    for (auto s:{"ospsnap\r\ntm\r\nScreenshot\r\n","Screenshot\r\ntm\r\nospsnap\r\ntfc 1\r\n","Screenshot\r\ntfc 1\r\nospsnap\r\n",
                 "player.setangle z 0\r\nScreenshot\r\nospsnap\r\n","Screenshot\r\nScreenshot\r\nospsnap\r\n"})
        CHECK(!check_bat(s,cell).ok);
    auto layout=[](std::vector<std::vector<std::string>> files){
        std::vector<std::pair<std::string,std::vector<std::string>>> v; int i=0;
        for (auto& f:files) v.push_back({"p"+std::to_string(i++),f});
        return check_layout(v);
    };
    CHECK(layout({{"tfc 1","ospsnap"},{"tm","ospsnap"},{"Screenshot","ospsnap"},{"tm","tfc 1","ospsnap"}}).ok);
    CHECK(!layout({{"tfc 1","ospsnap"},{"Screenshot","ospsnap"},{"tm","ospsnap"},{"tm","tfc 1","ospsnap"}}).ok);
    CHECK(!layout({{"tm","ospsnap"},{"Screenshot","ospsnap"},{"tm","ospsnap"}}).ok);
    CHECK(!layout({{"tfc 1","ospsnap"},{"tm","ospsnap"},{"Screenshot","ospsnap"}}).ok);
    for (auto s:{"tm\r\n","ospsnap\r\nospsnap\r\n","ospsnap\r\nsave x\r\n","ospsnap\r\ncoc Other\r\n","ospsnap\r\ntm\xe9\r\n",""})
        CHECK(!check_bat(s,cell).ok);
    std::string many="ospsnap\r\n"; for (int i=0;i<16;++i) many+="tm\r\n";
    CHECK(!check_bat(many,cell).ok);
    CHECK(!check_bat(std::string(5000,'x'),cell).ok);
}
static void paths() {
    for (auto s:{"run.json","nif_backface_cull_v1.signs-0123abcd-1","ScreenShot0.png"}) CHECK(safe_component(s));
    for (auto s:{"","..",".","../x","a\\b","a/b","C:","x:y",".hidden","trailing.","a b","\xc3\xa9"}) CHECK(!safe_component(s));
    CHECK(child(L"C:\\Game\\Data\\F4SE\\Plugins\\osp_measure\\runs",{"r1","run.json"})==std::wstring(L"C:\\Game\\Data\\F4SE\\Plugins\\osp_measure\\runs\\r1\\run.json"));
    CHECK(!child(L"C:\\base",{"..","x"}));
    CHECK(!child(L"C:\\base",{"r1","..\\..\\Windows"}));
    CHECK(!child(L"",{"x"}));
    CHECK(!child(L"C:\\base",{}));
    CHECK(console_text("a%b\x01")=="a?b?");
    CHECK(console_line("osp: next: bat t01b")=="osp: next: bat t01b\n" && console_line("a\nb")=="a?b\n");
    CHECK(std::string(f4se_message_name(2))=="PreLoadGame" && std::string(f4se_message_name(8))=="NewGame" &&
          std::string(f4se_message_name(10))=="GameDataReady" && std::string(f4se_message_name(11))=="unknown");
}
static void plans() {
    auto b=build_plan(std::string(64,'a'));
    std::string e; auto p=parse_plan(b.text,e);
    CHECK(p && e.empty());
    if (!p) return;
    CHECK(p->bench_file=="OSP Bench.esp" && p->plugins.back().second=="OSP Bench.esp" && p->ini_keys[0]=="iSize W:Display");
    CHECK(p->parts.size()==18 && p->steps()==2 && p->cell_local==0x1000);
    CHECK(p->exe_size==65503104 && p->exe_sha256==exe_sha);
    CHECK(p->parts[4].settle_seconds==40 && p->parts[4].simulated_required && p->parts[4].position[1]==0x461F6000);
    CHECK(p->parts[6].roots.size()==1 && p->parts[6].roots[0]==0x1034 && p->parts[6].free_seconds==3);
    CHECK(p->parts[7].role==Role::photo && p->parts[8].role==Role::repeat && p->parts[9].role==Role::restore);
    auto bad=[&](std::string text){ std::string why; return !parse_plan(text,why) && !why.empty(); };
    CHECK(bad(""));
    CHECK(bad("osp-plan 1\n"+b.text.substr(11)));       // photo_v1 plans are refused
    CHECK(bad(b.text.substr(0,b.text.size()-1)));
    std::string t=b.text; t.replace(t.find("end 18"),6,"end 19"); CHECK(bad(t));
    t=b.text; t.replace(t.find("exe 65503104 "),13,""); CHECK(bad(t));
    t=b.text; t.replace(t.find(" photo step1 1"),14," repeat step1 1"); CHECK(bad(t));
    t=b.text; t+="part extra "+std::string(64,'a')+" note - 0\n"; CHECK(bad(t));
    t=b.text; t.replace(t.find("plugin full OSP|Bench.esp\n"),26,""); CHECK(bad(t));
    t=b.text; t.replace(t.find(" settle step1 0 40"),18," settle step1 0 0"); CHECK(bad(t));
    // Setting names: an optional section (round 26 found bSaveOnTravel without one), never two.
    t=b.text; t.replace(t.find("ini iSize|W:Display\n"),20,"ini iSize|W:Display\nini bSaveOnTravel\nini SAntiAliasing:Display\n");
    { auto q=parse_plan(t,e); CHECK(q && q->ini_keys.size()==3 && q->ini_keys[1]=="bSaveOnTravel" && q->ini_keys[2]=="SAntiAliasing:Display"); }
    for (auto name:{"a:b:c",":Display","bSaveOnTravel:","1Key:Display","Key-x:Display","Key:Dis.play","|Key"}) {
        t=b.text; t.replace(t.find("ini iSize|W:Display\n"),20,std::string("ini ")+name+"\n"); CHECK(bad(t));
    }
    t=b.text; auto at=t.find("part t01b"); auto line=t.substr(at,t.find('\n',at)-at+1); t.erase(at,line.size());
    t.insert(t.find("part t01a"),line); CHECK(bad(t));
    t=b.text; t.replace(t.find("bench OSP|Bench.esp"),19,"bench ..|Bench.esp"); CHECK(bad(t));
    t=build_plan(std::string(64,'a'),"7F800000").text; CHECK(bad(t));
    t=b.text; t.replace(t.find("part t00a"),9,"part ../x"); CHECK(bad(t));
    t=b.text; t.replace(t.find("t01d "),5,"t01d\t"); CHECK(bad(t));
}
static Facts good_facts(const Built& b,const Plan& p) {
    Facts f; f.packet_sha256=p.packet_sha256; f.bench={true,p.bench_size,p.bench_sha256,""};
    f.executable={true,p.exe_size,p.exe_sha256,""};
    f.plugins=p.plugins;
    for (auto& [name,body]:b.files) f.parts.push_back({true,body.size(),osp::Sha256::of(body),body});
    return f;
}
static bool refused(const std::vector<Check>& c,const std::string& name) {
    for (auto& x:c) if (!x.passed) return x.name==name || x.name.rfind(name,0)==0;
    return false;
}
static void preflights() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    auto ok=preflight(p,good_facts(b,p));
    bool all=true; for (auto& c:ok) all=all&&c.passed; CHECK(all);
    auto f=good_facts(b,p); f.packet_sha256=std::string(64,'c'); CHECK(refused(preflight(p,f),"packet_sha256"));
    f=good_facts(b,p); f.packet_sha256.clear(); CHECK(refused(preflight(p,f),"packet_sha256"));
    f=good_facts(b,p); f.bench.sha256=std::string(64,'d'); CHECK(refused(preflight(p,f),"bench_plugin"));
    f=good_facts(b,p); f.bench.found=false; CHECK(refused(preflight(p,f),"bench_plugin"));
    f=good_facts(b,p); f.plugins->insert(f.plugins->begin()+1,{"full","Mod.esp"}); CHECK(refused(preflight(p,f),"active_plugins"));
    f=good_facts(b,p); f.plugins->back().first="light"; CHECK(refused(preflight(p,f),"active_plugins"));
    f=good_facts(b,p); f.plugins.reset(); CHECK(refused(preflight(p,f),"active_plugins"));
    f=good_facts(b,p); (*f.plugins)[0].second="FALLOUT4.ESM"; { bool a=true; for (auto& c:preflight(p,f)) a=a&&c.passed; CHECK(a); }
    // A part file whose bytes add a forbidden command is refused even before its hash.
    f=good_facts(b,p); f.parts[4].bytes="ospsnap\r\nsave quick\r\n"; CHECK(refused(preflight(p,f),"part t01b"));
    f=good_facts(b,p); f.parts[4].sha256=std::string(64,'e'); CHECK(refused(preflight(p,f),"part t01b"));
    f=good_facts(b,p); f.parts[6].bytes="ospsnap\r\ntm\r\nScreenshot\r\n"; CHECK(refused(preflight(p,f),"part t01d"));
    // The executable measured by round 25 is a requirement checked before any step.
    f=good_facts(b,p); f.executable.sha256=std::string(64,'0'); CHECK(refused(preflight(p,f),"executable"));
    f=good_facts(b,p); f.executable.size=1; CHECK(refused(preflight(p,f),"executable"));
    f=good_facts(b,p); f.executable.found=false; CHECK(refused(preflight(p,f),"executable"));
    // Layout from the actual bytes: a photo part moved before the interface is hidden.
    f=good_facts(b,p); std::swap(f.parts[6].bytes,f.parts[7].bytes);
    { auto c=preflight(p,f); bool layout_failed=false; for (auto& x:c) if (x.name=="layout") layout_failed=!x.passed; CHECK(layout_failed); }
    f=good_facts(b,p); f.parts.pop_back(); CHECK(refused(preflight(p,f),"part t99"));
    f=good_facts(b,p); f.in_world=true;
    { bool a=true; for (auto& c:preflight(p,f)) a=a&&c.passed; CHECK(a); CHECK(preflight(p,f).back().detail.find("loading a save")!=std::string::npos); }
}
static Observation at(double clock) { Observation o; o.clock=clock; o.bench_index=7; return o; }
static std::array<std::uint32_t,3> pose(const Plan& p,int step) { return p.parts[4+step_parts*step].position; }
using osp::Json;
// An observation as compact JSON text, for comparison.
static std::string observed(const Decision& d,const std::string& key) {
    for (auto& [k,v]:d.observed) if (k==key) { auto t=osp::encode(v); while (!t.empty() && t.back()=='\n') t.pop_back(); return t; }
    return "absent";
}
static bool failed(const Decision& d,const std::string& name) {
    for (auto& c:d.checks) if (c.name==name) return !c.passed;
    return false;
}
static void run_refusal_and_abort() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    auto f=good_facts(b,p); f.bench.sha256=std::string(64,'d');
    Run run(p); auto d=run.start(preflight(p,f),at(0));
    CHECK(run.status==Status::refused && d.outcome==Outcome::aborted && run.expected()==nullptr);
    CHECK(d.console[0].find("REFUSED before any step")!=std::string::npos);
    auto later=run.mark(at(1)); CHECK(later.outcome==Outcome::ignored && run.status==Status::refused);
    auto json=osp::encode(run_json(run,"p","s","r","t0","t1",osp::Json(),checks_json(d.checks),osp::Json::Array{}));
    CHECK(json.find("\"status\":\"refused\"")!=std::string::npos && json.find("bench_plugin")!=std::string::npos);
    CHECK(json.find("\"run_schema\":2")!=std::string::npos && json.find("\"f4se_messages\":[]")!=std::string::npos);
    Run other(p); other.start(preflight(p,good_facts(b,p)),at(0));
    other.abort("the game started a save during the run");
    CHECK(other.status==Status::aborted && other.reasons.back().find("save")!=std::string::npos);
    CHECK(other.mark(at(2)).outcome==Outcome::ignored);
    json=osp::encode(run_json(other,"p","s","r","t0","t1",osp::Json(),osp::Json::Array{},osp::Json::Array{}));
    CHECK(json.find("\"status\":\"aborted\"")!=std::string::npos && json.find("\"next_part\":null")!=std::string::npos);
}
// Every message is its own line and the last line names the next part alone.
static void console_messages() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    Run run(p); auto d=run.start(preflight(p,good_facts(b,p)),at(0));
    CHECK(d.console.size()==2 && d.console.back()=="osp: next: bat t00b");
    while (run.expected()) {
        auto o=at(1000.0*double(run.marks)); o.parent_cell=0x07001000; o.camera_state=3; o.camera_complete=true;
        o.position=pose(p,run.expected()->step=="step2"?1:0);
        auto name=run.expected()->name;
        d=run.mark(o);
        CHECK(d.outcome==Outcome::advanced);
        for (auto& line:d.console) CHECK(line.rfind("osp: ",0)==0 && line.find('\n')==std::string::npos);
        if (run.expected()) CHECK(d.console.back()=="osp: next: "+run.type_next());
        else CHECK(d.console.back().find("run complete")!=std::string::npos);
        if (name=="t01b") CHECK(d.console.size()==2);
    }
}
// Round 25, step 1, replayed with its own recorded words: the pose mark still saw
// the cell position, the first settle mark saw the pose 75.6 s later with 56.2 s of
// game time. Timed from the pose mark, that first settle is accepted.
static void round25_timing() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    Run run(p); run.start(preflight(p,good_facts(b,p)),at(58.306)); run.mark(at(104.509));
    auto o=at(143.012); o.parent_cell=0x07001000; run.mark(o);
    auto pose_mark=at(168.862); pose_mark.position=std::array<std::uint32_t,3>{f32(0),f32(-1024),f32(-8.00005054473877f)};
    pose_mark.game_days_bits=0x3EEDBE5A; pose_mark.timescale_bits=f32(20);
    auto d=run.mark(pose_mark);
    CHECK(d.outcome==Outcome::advanced && observed(d,"pose_in_effect_at_pose_mark")=="false");
    auto settle=at(244.511); settle.position=pose(p,0); settle.game_days_bits=0x3EF46614; settle.timescale_bits=f32(20);
    d=run.mark(settle);
    CHECK(d.outcome==Outcome::advanced && run.expected()->name=="t01c");
    CHECK(std::fabs(*d.wall_since-75.649)<0.01 && std::fabs(*d.simulated_since-56.15)<0.05);
    CHECK(observed(d,"pose_in_effect")=="true");
    CHECK(observed(d,"timing_origin")==R"({"mark":3,"role":"pose"})");
}
static void settle_scheduler_and_order() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    Run run(p); run.start(preflight(p,good_facts(b,p)),at(0));
    run.mark(at(1));                                          // note
    auto o=at(2); o.parent_cell=0x07001000; CHECK(run.mark(o).outcome==Outcome::advanced); // cell
    const float days0=2.5f, scale=20.0f;
    auto obs=[&](double clock,float days,int step){ auto x=at(clock); x.position=pose(p,step); x.game_days_bits=f32(days); x.timescale_bits=f32(scale); return x; };
    auto start=at(10); start.game_days_bits=f32(days0); start.timescale_bits=f32(scale);
    start.position=std::array<std::uint32_t,3>{f32(0),f32(-1024),f32(-8)};
    // Pose mark: the pose is not in effect yet (round 25); timing starts here anyway.
    auto d=run.mark(start);
    CHECK(d.outcome==Outcome::advanced && run.expected()->name=="t01b");
    CHECK(observed(d,"pose_in_effect_at_pose_mark")=="false");
    // Too early on the wall clock.
    d=run.mark(obs(30,days0+float(20*20/86400.0),0));
    CHECK(d.outcome==Outcome::not_settled && run.expected()->name=="t01b" && *d.wall_since==20);
    CHECK(d.console.back()=="osp: next: bat t01b");
    // Wall clock satisfied, but the console stayed open: no game time passed.
    d=run.mark(obs(60,days0+float(20*20/86400.0),0));
    CHECK(d.outcome==Outcome::not_settled && *d.simulated_since<40 && failed(d,"simulated_seconds"));
    // Both satisfied, still counted from the pose mark.
    d=run.mark(obs(70,days0+float(45*20/86400.0),0));
    CHECK(d.outcome==Outcome::advanced && *d.simulated_since>=40 && *d.wall_since==60 && run.expected()->name=="t01c");
    run.mark(at(71));                                         // free
    auto cap=at(73); cap.camera_state=3; cap.camera_complete=true;
    d=run.mark(cap);                                          // capture after 2 s of free camera: recorded, not refused
    CHECK(d.outcome==Outcome::advanced && d.read_roots && failed(d,"free_seconds"));
    run.mark(at(80)); run.mark(at(82)); run.mark(at(84));    // photo, repeat, restore
    // Step 2 typed out of order: the settle part arrives while standing at step 1's pose.
    CHECK(run.expected()->name=="t02a");
    run.mark(obs(85,days0,0));                                // pose mark: position not yet the new pose
    d=run.mark(obs(130,days0+0.02f,0));
    CHECK(d.outcome==Outcome::aborted && run.status==Status::aborted);
    CHECK(run.reasons.back().find("out of order")!=std::string::npos);
}
// The settle part typed before the pose part: the mark read as "pose" carried no
// pose, so the first settle mark does not see it and timing restarts there.
static void settle_before_pose() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    Run run(p); run.start(preflight(p,good_facts(b,p)),at(0)); run.mark(at(1));
    auto o=at(2); o.parent_cell=0x07001000; run.mark(o);
    auto cell_start=std::array<std::uint32_t,3>{f32(0),f32(-1024),f32(-8)};
    auto x=at(10); x.position=cell_start; run.mark(x);       // bat t01b typed first: read as the pose mark
    x=at(60); x.position=cell_start; auto d=run.mark(x);     // bat t01a typed now: read as a settle mark
    CHECK(d.outcome==Outcome::not_settled && observed(d,"timing_restarted")=="true");
    CHECK(d.console[0].find("not in effect")!=std::string::npos);
    x=at(80); x.position=pose(p,0); d=run.mark(x);            // 20 s after the real pose command
    CHECK(d.outcome==Outcome::not_settled && *d.wall_since==20);
    CHECK(observed(d,"timing_origin")==R"({"mark":4,"role":"settle"})");
    x=at(101); x.position=pose(p,0); d=run.mark(x);
    CHECK(d.outcome==Outcome::advanced && *d.wall_since==41 && !failed(d,"position"));
}
static void unavailable_game_time_and_pose_miss() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    Run run(p); run.start(preflight(p,good_facts(b,p)),at(0)); run.mark(at(1));
    auto o=at(2); o.parent_cell=0x07001000; run.mark(o);
    auto x=at(10); run.mark(x);                               // no globals, no position at the pose mark
    x=at(51); x.position=pose(p,0);
    auto d=run.mark(x);
    CHECK(d.outcome==Outcome::advanced && !d.simulated_since && observed(d,"timing_origin")==R"({"mark":3,"role":"pose"})");
    run.mark(at(52)); auto cap=at(56); cap.camera_state=3; cap.camera_complete=true; run.mark(cap);
    run.mark(at(57)); run.mark(at(58)); run.mark(at(59));
    // Step 2: the engine moved the player (collision): the pose is never attained.
    auto moved=pose(p,1); moved[2]=f32(9.5f);
    x=at(60); x.position=moved; run.mark(x);
    x=at(70); x.position=moved; d=run.mark(x);               // first miss: timing restarts here
    CHECK(d.outcome==Outcome::not_settled && observed(d,"timing_restarted")=="true");
    x=at(90); x.position=moved; d=run.mark(x);               // second miss, wait not yet complete
    CHECK(d.outcome==Outcome::not_settled && *d.wall_since==20);
    x=at(111); x.position=moved; d=run.mark(x);              // second miss after a full wait: give up, unqualified
    CHECK(d.outcome==Outcome::advanced && failed(d,"position"));
    bool warned=false; for (auto& line:d.console) warned=warned||line.find("WARNING")!=std::string::npos; CHECK(warned);
    run.mark(at(112));
    cap=at(116); cap.camera_state=0; cap.camera_complete=true;  // free camera never established
    d=run.mark(cap);
    CHECK(d.outcome==Outcome::aborted && run.reasons.back().find("free camera")!=std::string::npos);
}
static void cell_gate() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    Run run(p); run.start(preflight(p,good_facts(b,p)),at(0)); run.mark(at(1));
    auto o=at(2); o.parent_cell=0x00001000;
    CHECK(run.mark(o).outcome==Outcome::aborted && run.reasons.back().find("bench cell")!=std::string::npos);
}
// Walk a whole run; files[i] are revealed as the game would write them.
static osp::Json simulate(const Plan& p,const std::vector<ShotFile>& files,Run& run) {
    osp::Json::Array marks; std::size_t reveal=0;
    std::vector<ShotFile> later;
    auto camera=[](int state){
        auto w=[](float v){ return word_json(f32(v)); };
        auto row=[&](float a,float b,float c){ return osp::Json::Array{w(a),w(b),w(c)}; };
        osp::Json::Array m; for (int i=0;i<16;++i) m.push_back(w(0));
        return osp::Json::object({{"complete",true},{"state_id",state},{"world_fov_raw",w(70)},{"first_person_fov_raw",w(80)},
            {"camera",osp::Json::object({{"world",osp::Json::object({{"rotation",osp::Json::Array{row(0,1,0),row(0,0,1),row(1,0,0)}},
                {"rotation_padding",row(0,0,0)},{"translation",row(8192,10200,7.17578125f)},{"scale",w(1)}})},
                {"world_to_camera_raw",m},{"frustum_left_right_top_bottom_near_far",osp::Json::Array{w(-0.8391f),w(0.8391f),w(0.472f),w(-0.472f),w(15),w(353840)}},
                {"viewport_left_right_top_bottom",osp::Json::Array{w(0),w(1),w(1),w(0)}},{"orthographic",false}})},{"errors",osp::Json::Array{}}});
    };
    double clock=0; float days=3.0f; int step=-1;
    auto start=run.start(std::vector<Check>{{"all",true,"simulated"}},at(clock));
    marks.push_back(mark_json(0,start,at(clock),"2026-11-16T00:00:00.000Z",osp::Json(),osp::Json::Array{},start.console));
    while (run.expected()) {
        const Part& part=*run.expected();
        if (part.role==Role::pose) ++step;
        clock+=part.role==Role::settle?41:part.role==Role::capture?4:1;
        if (part.role==Role::settle) days+=float(42*20/86400.0);
        auto o=at(clock); o.parent_cell=(7u<<24)|p.cell_local; o.game_days_bits=f32(days); o.timescale_bits=f32(20);
        if (step>=0) o.position=pose(p,step);
        osp::Json cam;
        if (run.wants_camera()) { cam=camera(3); apply_camera(cam,o); }
        auto d=run.mark(o);
        osp::Json::Array r;
        if (d.read_roots) for (auto id:part.roots) r.push_back(osp::Json::object({{"form_id",osp::hex((7u<<24)|id)},{"complete",true},{"world",osp::Json()},{"errors",osp::Json::Array{}}}));
        // Each photograph is written after its part has run: seen at the following mark.
        std::vector<ShotFile> now=later; later.clear();
        if ((part.role==Role::photo || part.role==Role::repeat) && reveal<files.size()) later.push_back(files[reveal++]);
        run.bind(now);
        marks.push_back(mark_json(marks.size(),d,o,"2026-11-16T00:00:00.000Z",cam,r,d.console,
                                  osp::Json::object({{"deferred",osp::Json::Array{}}})));
    }
    return marks;
}
static void camera_fields() {
    Observation o; o.camera_state=9; o.camera_complete=true;
    apply_camera(osp::Json(),o); CHECK(!o.camera_state && !o.camera_complete);
    apply_camera(osp::Json::object({{"complete",true},{"state_id",std::uint32_t(3)}}),o);
    CHECK(o.camera_complete && o.camera_state && *o.camera_state==3);
    apply_camera(osp::Json::object({{"complete",false},{"state_id",nullptr}}),o);
    CHECK(!o.camera_complete && !o.camera_state);
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    Run run(p); CHECK(!run.wants_camera()); run.start(preflight(p,good_facts(b,p)),at(0));
    std::vector<bool> wants;
    while (run.expected()) {
        wants.push_back(run.wants_camera());
        auto o2=at(1000.0*double(wants.size())); o2.parent_cell=0x07001000; o2.position=pose(p,0); o2.camera_state=3; o2.camera_complete=true;
        if (run.expected()->step=="step2") o2.position=pose(p,1);
        run.mark(o2);
    }
    CHECK((wants==std::vector<bool>{false,false,false,true,false,true,true,true,true,false,true,false,true,true,true,true,false}));
}
static void binding() {
    auto b=build_plan(std::string(64,'a')); auto p=parsed(b.text);
    std::vector<ShotFile> files; for (int i=0;i<4;++i) files.push_back({"ScreenShot"+std::to_string(i)+".png",100u+i,std::string(64,char('0'+i))});
    Run run(p); simulate(p,files,run);
    CHECK(run.status==Status::complete && run.pending.empty() && run.shots.size()==4);
    CHECK(run.shots[0].step=="step1" && run.shots[0].shot=="photo" && run.shots[1].shot=="repeat" && run.shots[3].step=="step2");
    for (auto& s:run.shots) CHECK(s.status=="bound");
    run.bind({{"ScreenShot9.png",1,std::string(64,'9')}});
    CHECK(run.shots.back().status=="unexpected");
    CHECK(run.mark(at(999)).outcome==Outcome::ignored);
    Run missing(p); simulate(p,{files[0],files[1],files[2]},missing);
    CHECK(missing.pending.size()==1 && missing.pending[0].step=="step2" && missing.pending[0].shot=="repeat");
}
static std::string read_file(const char* path) { std::ifstream f(path,std::ios::binary); std::ostringstream o; o<<f.rdbuf(); return o.str(); }

int main(int argc,char** argv) {
    std::string mode=argc>1?argv[1]:"";
    if (mode=="commands") {          // one verdict per stdin line, for the shared corpus
        std::string line; while (std::getline(std::cin,line)) std::cout<<(check_command(line,cell).ok?"ok":"no")<<"\n";
        return 0;
    }
    if (mode=="ini") {               // one verdict per stdin line: an escaped plan ini field
        std::string line; while (std::getline(std::cin,line)) std::cout<<(plan_name(line) && ini_name(unescape(line))?"ok":"no")<<"\n";
        return 0;
    }
    if (mode=="sha256") { std::ostringstream o; o<<std::cin.rdbuf(); std::cout<<osp::Sha256::of(o.str())<<"\n"; return 0; }
    if (mode=="plan" && argc==3) {
        std::string e; auto p=parse_plan(read_file(argv[2]),e);
        if (!p) { std::cout<<osp::encode(osp::Json::object({{"ok",false},{"error",e}})); return 0; }
        osp::Json::Array parts;
        for (auto& x:p->parts) parts.push_back(osp::Json::Array{x.name,x.sha256,role_name(x.role),x.step,x.shots});
        std::cout<<osp::encode(osp::Json::object({{"ok",true},{"id",p->id},{"packet_sha256",p->packet_sha256},{"bench",p->bench_file},{"plugins",p->plugins.size()},{"ini",p->ini_keys.size()},{"parts",parts}}));
        return 0;
    }
    if (mode=="simulate" && argc==4) {  // plan file, then lines "name size sha256" in shot order
        std::string e; auto p=parse_plan(read_file(argv[2]),e);
        if (!p) { std::cerr<<e<<"\n"; return 2; }
        std::vector<ShotFile> files; std::istringstream in(read_file(argv[3])); ShotFile f;
        while (in>>f.name>>f.size>>f.sha256) files.push_back(f);
        Run run(*p); auto marks=simulate(*p,files,run);
        auto env=osp::Json::object({{"game_version","1.10.163.0"},{"f4se_version","0.6.23"},{"f4se_release_index",25},{"plugin_version",9},
            {"errors",osp::Json::object({})}});
        std::cout<<osp::encode(run_json(run,std::string(64,'f'),"SIMULATED","sim-1","t0","t1",env,osp::Json::Array{},marks));
        return 0;
    }
    sha256_vectors(); closed_list(); paths(); plans(); preflights(); run_refusal_and_abort();
    console_messages(); round25_timing(); settle_scheduler_and_order(); settle_before_pose();
    unavailable_game_time_and_pose_miss(); cell_gate(); camera_fields(); binding();
    if (failures) { std::fprintf(stderr,"%d sequencer check(s) failed\n",failures); return 1; }
    std::puts("PASS: sequencer SHA-256, closed list, Screenshot isolation, layout, path guard, plan format 2 incl. setting names without a section, refusal checks incl. executable, console lines, round 25 timing replay, settle from the pose mark, order, abort, cell gate, camera state, screenshot binding");
    return 0;
}
