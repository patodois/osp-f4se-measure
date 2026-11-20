// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include "osp/observe.hpp"
#include <set>
#include <cstring>

// Independent byte views of named SDK fields. See docs/provenance.md.
namespace osp::nodes {
using layout::Address;
constexpr Address lookup_form_rva=0x00152c90;
constexpr std::size_t actor_root_slot=0x8b, object_root_slot=0x8c;
constexpr std::size_t rtti_slot=2, as_node_slot=3, as_geometry_slot=8;
struct Transform { std::uint32_t words[16]; };
struct Object {
    std::byte prefix[0x10];
    Address name, controller, extra_data, parent;
    Transform local, world;
};
struct Children {
    Address vtable, entries;
    std::uint16_t capacity, end, size, grow;
};
struct StringEntry {
    Address next;
    std::uint32_t state,length;
    Address external;
};
static_assert(sizeof(Transform)==0x40 && sizeof(Object)==0xb0);
static_assert(offsetof(Object,name)==0x10 && offsetof(Object,parent)==0x28);
static_assert(offsetof(Object,local)==0x30 && offsetof(Object,world)==0x70);
static_assert(sizeof(Children)==0x18 && offsetof(Children,entries)==8 && offsetof(Children,end)==0x12);
static_assert(sizeof(StringEntry)==0x18 && offsetof(StringEntry,state)==8 && offsetof(StringEntry,external)==0x10);
struct Request { std::string tag; std::uint32_t form_id; };
inline std::optional<Request> request(const std::string& text) {
    // This syntax extends only inputs rejected by the v5 control parser.
    auto at=text.find('\n');
    if(at==std::string::npos) return std::nullopt;
    auto tag=text.substr(0,at), rest=text.substr(at+1);
    if(!tag.empty() && tag.back()=='\r') tag.pop_back();
    if(!rest.empty() && rest.back()=='\n') rest.pop_back();
    if(!rest.empty() && rest.back()=='\r') rest.pop_back();
    if(tag.size()>160 || rest.size()!=14 || rest.substr(0,6)!="nodes=") return std::nullopt;
    for(unsigned char c:tag) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':')) return std::nullopt;
    std::uint32_t id=0;
    for(char c:rest.substr(6)) {
        unsigned d=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):c>='A'&&c<='F'?unsigned(c-'A'+10):16;
        if(d==16) return std::nullopt;
        id=(id<<4)|d;
    }
    if(!id) return std::nullopt;
    return Request{tag,id};
}
inline Json scalar(std::uint32_t bits) {
    Json decimal;
    // Classify the exponent as integers first: even a signalling NaN in an
    // unused rotation lane must not perform a floating operation in the game.
    if((bits&0x7f800000u)!=0x7f800000u) {
        float value=0;std::memcpy(&value,&bits,4);decimal=Json(double(value));
    }
    return Json::object({{"bits",hex(bits)},{"decimal",decimal}});
}
inline Json transform(const Transform& t) {
    Json::Array rotation,padding,translation;
    for(unsigned i=0;i<3;++i) {
        Json::Array row;
        for(unsigned j=0;j<3;++j) row.push_back(scalar(t.words[i*4+j]));
        rotation.emplace_back(row); padding.push_back(scalar(t.words[i*4+3]));
        translation.push_back(scalar(t.words[12+i]));
    }
    return Json::object({{"rotation",rotation},{"translation",translation},{"scale",scalar(t.words[15])},
        {"rotation_padding",padding}});
}
struct Limits { std::size_t nodes=4096,depth=128,children=8192,bones=2048,bone_names=32768,stops=128; };
template<class T,class Reader> std::optional<T> field(Reader& reader,Address base,std::size_t offset) {
    if(!address_range(base,offset+sizeof(T))) return std::nullopt;
    return read_at<Reader,T>(reader,base+offset);
}
template<class Reader> std::optional<std::string> fixed_name(Reader& reader,Address entry) {
    if(!entry) return std::string();
    std::set<Address> seen;
    for(unsigned depth=0;depth<32;++depth) {
        if(!entry || !seen.insert(entry).second) return std::nullopt;
        auto header=read_at<Reader,StringEntry>(reader,entry);
        if(!header) return std::nullopt;
        if(header->state&0x4000) {entry=header->external;continue;}
        if(!(header->state&0x8000)) {
            if(!address_range(entry,sizeof(StringEntry)+1)) return std::nullopt;
            return read_string(reader,entry+sizeof(StringEntry),1024);
        }
        std::string out;
        for(std::size_t i=0;i<1024;++i) {
            auto unit=field<std::uint16_t>(reader,entry,sizeof(StringEntry)+i*2);
            if(!unit) return std::nullopt;
            std::uint32_t cp=*unit;
            if(!cp) return out;
            if(cp>=0xd800 && cp<=0xdbff) {
                if(++i>=1024) return std::nullopt;
                auto low=field<std::uint16_t>(reader,entry,sizeof(StringEntry)+i*2);
                if(!low || *low<0xdc00 || *low>0xdfff) return std::nullopt;
                cp=0x10000+((cp-0xd800)<<10)+(*low-0xdc00);
            } else if(cp>=0xdc00 && cp<=0xdfff) return std::nullopt;
            if(cp<=0x7f) out.push_back(char(cp));
            else if(cp<=0x7ff) {out.push_back(char(0xc0|(cp>>6)));out.push_back(char(0x80|(cp&63)));}
            else if(cp<=0xffff) {out.push_back(char(0xe0|(cp>>12)));out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));}
            else {out.push_back(char(0xf0|(cp>>18)));out.push_back(char(0x80|((cp>>12)&63)));out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));}
        }
        return std::nullopt;
    }
    return std::nullopt;
}
template<class Reader,class Calls> Json tree(Reader& reader,Calls& calls,Address root,Limits limits={}) {
    struct Pending {Address address; std::int64_t parent; std::size_t child,depth; Address expected_parent;};
    std::vector<Pending> todo{{root,-1,0,0,0}};
    std::set<Address> visited;
    Json::Array rows,stops;bool halted=false,hierarchy_complete=true,names_complete=true,types_complete=true,skins_complete=true;std::size_t bone_names=0;
    auto stop=[&](const Pending& p,const std::string& why,const std::string& scope="hierarchy") {
        if(scope=="skin") skins_complete=false;
        else if(scope=="name") names_complete=false;
        else if(scope=="type") types_complete=false;
        else hierarchy_complete=false;
        stops.push_back(Json::object({{"parent_index",p.parent<0?Json():Json(p.parent)},
            {"child_slot",p.child},{"depth",p.depth},{"address",hex(p.address,16)},
            {"reason",stops.size()+1>=limits.stops?"diagnostic_limit":why},{"original_reason",why},{"scope",scope}}));
        if(stops.size()>=limits.stops) {halted=true;hierarchy_complete=false;}
    };
    while(!todo.empty() && !halted) {
        auto p=todo.back();todo.pop_back();
        if(!p.address) {stop(p,"null_root");continue;}
        if(p.depth>limits.depth) {stop(p,"depth_limit");continue;}
        if(rows.size()>=limits.nodes) {stop(p,"node_limit");break;}
        if(!visited.insert(p.address).second) {stop(p,"cycle_or_shared_node");continue;}
        auto obj=read_at<Reader,Object>(reader,p.address);
        if(!obj) {stop(p,"unreadable_object");continue;}
        auto name=fixed_name(reader,obj->name);
        if(!name) stop(p,"unreadable_or_long_name","name");
        auto rt=calls.get(p.address,rtti_slot), nn=calls.get(p.address,as_node_slot);
        std::optional<std::string> type;
        if(rt && *rt) {
            auto np=read_at<Reader,Address>(reader,*rt);
            if(np && *np) type=read_string(reader,*np,256);
        }
        if(!type) stop(p,"rtti_unavailable","type");
        if(!nn) stop(p,"node_classification_call_failed");
        Json::Array bones;
        bool skin_complete=true,skin_present=false;std::size_t missing_bones=0;
        auto geometry=calls.get(p.address,as_geometry_slot);
        if(!geometry) {stop(p,"geometry_classification_call_failed","skin");skin_complete=false;}
        else if(*geometry) {
            auto skin=field<Address>(reader,*geometry,0x140);
            if(!skin) {stop(p,"skin_pointer_unreadable","skin");skin_complete=false;}
            else if(*skin) {
                skin_present=true;
                auto array=field<layout::Array>(reader,*skin,0x10);
                if(!array || array->count>array->capacity || array->count>limits.bones || (array->count && !array->entries)) {
                    stop(p,"skin_bone_array_invalid_or_limit","skin");skin_complete=false;
                } else for(std::size_t i=0;i<array->count && !halted;++i) {
                    if(bone_names>=limits.bone_names) {skin_complete=false;stop(p,"total_skin_bone_limit","skin");break;}
                    ++bone_names;
                    auto bone=field<Address>(reader,array->entries,i*8);
                    auto np=bone && *bone?field<Address>(reader,*bone,0x10):std::nullopt;
                    auto bn=np?fixed_name(reader,*np):std::nullopt;
                    bones.push_back(Json::object({{"slot",i},{"address",bone?Json(hex(*bone,16)):Json()},
                        {"name",bn?Json(*bn):Json()}}));
                    if(!bn) {skin_complete=false;++missing_bones;}
                }
                if(missing_bones) stop(p,"skin_bone_or_name_unreadable","skin");
            }
        }
        auto index=rows.size();
        Json parent_world;
        if(p.parent<0 && obj->parent) {
            auto po=read_at<Reader,Object>(reader,obj->parent);
            if(po) parent_world=transform(po->world); else stop(p,"external_parent_unreadable");
        }
        if(p.parent>=0 && obj->parent!=p.expected_parent) stop(p,"parent_link_mismatch");
        rows.push_back(Json::object({{"index",index},{"parent_index",p.parent<0?Json():Json(p.parent)},
            {"child_slot",p.child},{"depth",p.depth},{"address",hex(p.address,16)},
            {"parent_address",hex(obj->parent,16)},{"name",name?Json(*name):Json()},
            {"type",type?Json(*type):Json()},{"kind",nn?(*nn?Json("node"):Json("leaf")):Json("unknown")},
            {"local",transform(obj->local)},{"world",transform(obj->world)},
            {"external_parent_world",parent_world},{"skin_present",skin_present},{"skin_complete",skin_complete},
            {"skin_missing_names",missing_bones},{"skin_bones",bones}}));
        if(nn && *nn) {
            auto children=field<Children>(reader,*nn,0x120);
            if(!children || children->end>children->capacity || children->size>children->end ||
               children->end>limits.children || (children->end && !children->entries)) {stop(p,"children_invalid_or_limit");continue;}
            if(todo.size()+children->end>limits.nodes) {stop(p,"pending_node_limit");continue;}
            for(std::size_t i=children->end;i>0 && !halted;--i) {
                auto child=field<Address>(reader,children->entries,(i-1)*8);
                if(!child) stop(Pending{0,std::int64_t(index),i-1,p.depth+1,p.address},"child_pointer_unreadable");
                else if(*child) todo.push_back({*child,std::int64_t(index),i-1,p.depth+1,p.address});
            }
        }
    }
    return Json::object({{"root_address",hex(root,16)},{"complete",stops.empty()},
        {"transforms_and_hierarchy_complete",hierarchy_complete},{"names_complete",names_complete},
        {"types_complete",types_complete},{"skins_complete",skins_complete},
        {"nodes",rows},{"stops",stops},{"limits",Json::object({{"nodes",limits.nodes},{"depth",limits.depth},
            {"children",limits.children},{"bones",limits.bones},{"bone_names",limits.bone_names},{"stops",limits.stops}})}});
}
}
