// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include "osp/scene.hpp"
#include "osp/settings.hpp"
#include <map>
#include <stdexcept>

// Independently written schema-3 validation. Layout sources remain those cited
// in docs/provenance.md. No engine floating-point environment is changed.
namespace osp::scene3 {
using nodes::Transform;
using layout::Address;
struct Expected { std::int32_t parent; std::string name; };
struct View { std::string root_name; std::vector<Expected> bones; };
struct Forecast { std::string id; View third,first; bool complete=false; std::string error; };
inline std::string fold(std::string s) {
    for(char& c:s) if(c>='A'&&c<='Z') c=char(c-'A'+'a');
    return s;
}
inline Forecast forecast(const std::string& bytes) {
    Forecast f;std::uint64_t id=14695981039346656037ull;
    for(unsigned char c:bytes) {id^=c;id*=1099511628211ull;}
    f.id=hex(id,16);
    try {
        if(bytes.size()>512*1024 || bytes.size()<16 || bytes.substr(0,8)!="OSPB19V1") throw std::runtime_error("invalid_forecast_header");
        std::size_t at=8;
        auto number=[&](unsigned n) {if(at+n>bytes.size()) throw std::runtime_error("truncated_forecast");
            std::uint32_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint32_t(static_cast<unsigned char>(bytes[at++]))<<(8*i);return v;};
        auto text=[&]() {auto n=number(2);if(n>1024 || at+n>bytes.size())throw std::runtime_error("invalid_forecast_name");
            auto s=bytes.substr(at,n);at+=n;for(unsigned char c:s)if(c<0x20 || c>0x7e)throw std::runtime_error("forecast_requires_printable_ASCII_names");return s;};
        const auto third=number(4),first=number(4);
        for(auto pair:{std::pair<View*,std::uint32_t>{&f.third,third},{&f.first,first}}) {
            auto& v=*pair.first;const auto count=pair.second;
            if(!count || count>2048)throw std::runtime_error("forecast_count_limit");
            v.root_name=text();if(v.root_name.empty())throw std::runtime_error("forecast_root_missing");
            std::set<std::string> seen;
            for(std::uint32_t i=0;i<count;++i) {
                const auto raw=number(4);const std::int32_t p=raw==0xffffffffu?-1:raw<=2047?std::int32_t(raw):-2;
                if(p< -1 || p>=std::int32_t(i))throw std::runtime_error("forecast_parent_not_earlier");
                auto name=text();if(!seen.insert(fold(name)).second)throw std::runtime_error("forecast_duplicate_folded_name");
                v.bones.push_back({p,std::move(name)});
            }
        }
        if(at!=bytes.size())throw std::runtime_error("forecast_trailing_bytes");
        f.complete=true;
    } catch(const std::exception& e) {f.error=e.what();}
    return f;
}
struct Request { nodes::Request base; std::uint32_t pause; };
inline std::optional<Request> request(const std::string& text) {
    auto line=text.find("\nnodes3=");if(line==std::string::npos)return std::nullopt;
    auto rest=text.substr(line+8);if(rest.size()!=24 || rest[8]!='\n' || rest.substr(9,6)!="pause=" || rest.back()!='\n')return std::nullopt;
    auto base=nodes::request(text.substr(0,line)+"\nnodes="+rest.substr(0,8)+"\n");if(!base)return std::nullopt;
    std::uint32_t pause=0;for(char c:rest.substr(15,8)) {unsigned n=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:16;if(n==16)return std::nullopt;pause=(pause<<4)|n;}
    if(!pause)return std::nullopt;
    return Request{*base,pause};
}
// Decode float words into NORMAL double values even when the engine enables
// DAZ. Round to float words in integers, independently of MXCSR FTZ/DAZ flags.
inline double value(std::uint32_t w) {
    auto e=(w>>23)&255u,frac=w&0x7fffffu;
    if(e==255)return std::numeric_limits<double>::quiet_NaN();
    double v=e?std::ldexp(double(0x800000u|frac),int(e)-150):std::ldexp(double(frac),-149);
    return w&0x80000000u?-v:v;
}
inline std::uint64_t rounded_shift(std::uint64_t m,unsigned n) {
    if(n>=64)return 0;
    if(!n)return m;
    auto q=m>>n,rest=m&((1ull<<n)-1),half=1ull<<(n-1);
    return q+(rest>half || (rest==half && (q&1)));
}
inline std::uint32_t rounded(double v,bool ftz) {
    std::uint64_t u=0;std::memcpy(&u,&v,8);const auto sign=std::uint32_t(u>>32)&0x80000000u;
    const unsigned de=unsigned((u>>52)&2047u);if(de==2047)return sign|0x7f800000u;
    if(!(u&0x7fffffffffffffffull))return sign;
    if(!de)return sign; // impossible for finite float32 add/multiply operands
    int e=int(de)-1023;auto m=(u&0xfffffffffffffull)|(1ull<<52);
    if(e>127)return sign|0x7f800000u;
    if(e< -126) {auto q=rounded_shift(m,unsigned(29+(-126-e)));return sign|((ftz && q<0x800000u)?0u:std::uint32_t(q));}
    auto q=rounded_shift(m,29);if(q==0x1000000u){q>>=1;++e;}
    if(e>127)return sign|0x7f800000u;
    return sign|(std::uint32_t(e+127)<<23)|(std::uint32_t(q)&0x7fffffu);
}
inline std::uint32_t mul(std::uint32_t a,std::uint32_t b,bool ftz) {return rounded(value(a)*value(b),ftz);}
inline std::uint32_t add(std::uint32_t a,std::uint32_t b,bool ftz) {return rounded(value(a)+value(b),ftz);}
inline std::uint32_t dot(const std::uint32_t* a,const std::uint32_t* b,bool ftz) {
    auto out=mul(a[0],b[0],ftz);for(unsigned i=1;i<3;++i)out=add(out,mul(a[i],b[i],ftz),ftz);return out;
}
inline Transform compose(const Transform& p,const Transform& l,bool ftz) {
    Transform out{};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        std::uint32_t a[3]={l.words[i*4],l.words[i*4+1],l.words[i*4+2]},b[3]={p.words[j],p.words[4+j],p.words[8+j]};out.words[i*4+j]=dot(a,b,ftz);
    }
    for(unsigned i=0;i<3;++i) {std::uint32_t a[3]={p.words[i],p.words[4+i],p.words[8+i]};out.words[12+i]=add(p.words[12+i],mul(p.words[15],dot(a,l.words+12,ftz),ftz),ftz);}
    out.words[15]=mul(p.words[15],l.words[15],ftz);return out;
}
inline bool equal(const Transform& a,const Transform& b,bool padding=false) {
    for(unsigned i=0;i<16;++i)if((padding || i%4!=3 || i==15) && a.words[i]!=b.words[i])return false;
    return true;
}
inline bool finite(const Transform& t) {
    for(unsigned i=0;i<16;++i)if((i%4!=3 || i==15) && (t.words[i]&0x7f800000u)==0x7f800000u)return false;
    return true;
}
inline Json array_words(const Transform& t) {
    Json::Array a;for(unsigned i=0;i<16;++i)if(i%4!=3 || i==15)a.emplace_back(hex(t.words[i]));return a;
}
struct Proof { bool names=false,parents=false,common=false,ftz=false,strict=false,readable=false;Json json;bool accepted()const{return names&&parents&&common&&ftz&&readable;} };
template<class Reader,class Calls>
Proof enrich(Reader& reader,Calls& calls,Json& flat,const Json& ordinary,const View& expected) {
    Proof result;Json::Array errors,pointers,composition;std::vector<scene::Bone> bones;std::vector<Transform> locals,worlds;
    auto address=scene::address(scene::member(flat,"address"));auto root=address?read_at<Reader,nodes::Object>(reader,*address):std::nullopt;
    auto root_name=root?nodes::fixed_name(reader,root->name):std::nullopt;
    auto data=address?nodes::field<nodes::Address>(reader,*address,0x148):std::nullopt;
    auto rows=osp::field(flat,"bones");auto array=rows?std::get_if<Json::Array>(&rows->data):nullptr;
    std::set<Address> allowed;
    if(auto ns=scene::member(ordinary,"nodes"))if(auto a=std::get_if<Json::Array>(&ns->data))for(const auto& n:*a)if(auto p=scene::address(scene::member(n,"address")))allowed.insert(*p);
    auto count=address?nodes::field<std::int32_t>(reader,*address,0x140):std::nullopt;
    auto expanded=address?nodes::field<std::int32_t>(reader,*address,0x144):std::nullopt;
    // Schema2's unrelated bool-byte range diagnostic stays unchanged. The
    // four schema3 proofs independently check their own published fields.
    bool valid=count && expanded && *count>=0 && *count<=2048 && *expanded>=0 && *expanded<=2048 && array && std::size_t(*count)==array->size() && flag(ordinary,"transforms_and_hierarchy_complete") && root && root_name && fold(*root_name)==fold(expected.root_name) && data && *data && array && array->size()==expected.bones.size();
    if(!valid)errors.emplace_back("forecast_or_readable_tree_mismatch");
    result.names=valid;result.parents=valid;result.common=valid;
    if(valid)for(std::size_t i=0;i<array->size();++i) {
        auto b=nodes::field<scene::Bone>(reader,*data,i*sizeof(scene::Bone));if(!b){errors.emplace_back("bone_reread_failed");valid=false;break;}
        auto name=nodes::fixed_name(reader,b->name);const auto& e=expected.bones[i];bool case_equal=name && fold(*name)==fold(e.name);
        if(!name) {valid=false;errors.emplace_back("bone_name_reread_failed");}
        Json case_probe;
        if(name && *name!=e.name) {
            layout::FixedString key{};bool constructed=calls.construct(&key,e.name.c_str());Address entry=key.entry;
            // Release before allocating JSON or an error string. Balance even a
            // partially initialized key after a guarded constructor fault.
            bool released=(constructed || entry)?calls.release(&key):false;
            bool match=constructed && entry && entry==b->name && released;
            case_equal=case_equal && match;
            case_probe=Json::object({{"file_spelling",e.name},{"runtime_spelling",*name},{"constructed",constructed},{"constructed_entry",hex(entry,16)},{"record_entry",hex(b->name,16)},{"entry_equal",entry==b->name},{"released",released},{"passed",match}});
            pointers.push_back(case_probe);
        }
        result.names=result.names && case_equal;result.parents=result.parents && b->parent==e.parent;
        std::optional<nodes::Object> common;bool same=true;
        if(b->node) {
            if(allowed.count(b->node))common=read_at<Reader,nodes::Object>(reader,b->node);
            same=common && common->name==b->name && equal(common->world,b->world,true);
            result.common=result.common && same;
        }
        auto effective=common?common->local:b->local;auto world=common?common->world:b->world;
        if((b->node && !common) || !finite(effective) || !finite(world)) {valid=false;errors.emplace_back("effective_transform_unavailable");}
        auto& pairs=std::get<Json::Object>((*array)[i].data);
        pairs.emplace_back("name_entry_address",hex(b->name,16));pairs.emplace_back("expected_name",e.name);pairs.emplace_back("expected_parent_index",e.parent);
        pairs.emplace_back("name_order_equal_folded",case_equal);pairs.emplace_back("case_probe",case_probe);
        pairs.emplace_back("node_in_ordinary_tree",b->node?Json(bool(allowed.count(b->node))):Json());
        pairs.emplace_back("node_name_entry_address",common?Json(hex(common->name,16)):Json());pairs.emplace_back("common_world_equal_all_words",b->node?Json(same):Json());
        pairs.emplace_back("node_local",common?nodes::transform(common->local):Json());pairs.emplace_back("node_world",common?nodes::transform(common->world):Json());
        pairs.emplace_back("effective_local_source",b->node?"ordinary_node":"flattened_record");pairs.emplace_back("effective_local",nodes::transform(effective));pairs.emplace_back("effective_world",nodes::transform(world));
        bones.push_back(*b);locals.push_back(effective);worlds.push_back(world);
    }
    result.readable=valid;result.ftz=valid;result.strict=valid;
    if(valid)for(std::size_t i=0;i<bones.size();++i) {
        auto parent=bones[i].parent;bool good=parent>=-1 && parent<std::int32_t(i);
        Transform p=good?(parent<0?root->world:worlds[std::size_t(parent)]):Transform{};
        auto strict=compose(p,locals[i],false),ftz=compose(p,locals[i],true);
        bool es=good && finite(p) && equal(strict,worlds[i]),ef=good && finite(p) && equal(ftz,worlds[i]);result.strict=result.strict&&es;result.ftz=result.ftz&&ef;
        composition.push_back(Json::object({{"index",i},{"parent_index",parent},{"without_ftz_equal",es},{"with_ftz_equal",ef},{"predicted_without_ftz_words",array_words(strict)},{"predicted_with_ftz_words",array_words(ftz)}}));
    }
    result.json=Json::object({{"complete",valid},{"names_and_order",result.names},{"parent_indices",result.parents},{"common_pointer_name_and_world",result.common},{"composition_ftz",result.ftz},{"composition_without_ftz",result.strict},{"accepted",result.accepted()},{"case_probes",pointers},{"composition",composition},{"errors",errors},{"arithmetic","R17 sequential float32; subnormal RESULTS become sign-preserving zero; inputs decoded without DAZ; no MXCSR changes"}});
    auto& pairs=std::get<Json::Object>(flat.data);pairs.emplace_back("round19_proofs",result.json);return result;
}
struct Tracker {
    std::string forecast_id;std::vector<std::uint32_t> pauses;
    bool observe(const std::string& id,std::uint32_t pause,bool player,bool applicable,bool passed) {
        if(id!=forecast_id) {forecast_id=id;pauses.clear();}
        if((applicable || player) && !passed)pauses.clear();
        if(player && passed && (pauses.empty() || pause>pauses.back()) && pauses.size()<2)pauses.push_back(pause);
        return pauses.size()>=2 && (!applicable || passed);
    }
};
}
