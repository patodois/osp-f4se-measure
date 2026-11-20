// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include "osp/nodes.hpp"
#include <array>

// Layout facts only: LucaDotGit/CommonLibF4 e337150, MIT, credited with the
// complete license and exact source lines in docs/provenance.md. No code from
// that library is compiled or included. The runtime proofs remain external.
namespace osp::scene {
using layout::Address;
inline constexpr Address camera_rva=0x058ceb28; // F4SE GameCamera.cpp:4
inline const Json* member(const Json& j,const std::string& name) {
    auto object=std::get_if<Json::Object>(&j.data);
    if(object) for(const auto& p:*object) if(p.first==name) return &p.second;
    return nullptr;
}
inline std::optional<std::string> string(const Json* j) {
    if(j) if(auto s=std::get_if<std::string>(&j->data)) return *s;
    return std::nullopt;
}
inline std::optional<Address> address(const Json* j) {
    auto s=string(j);if(!s || s->size()!=16) return std::nullopt;
    Address out=0;
    for(char c:*s) {unsigned x=c>='0'&&c<='9'?unsigned(c-'0'):c>='A'&&c<='F'?unsigned(c-'A'+10):16;
        if(x==16) return std::nullopt;
        out=(out<<4)|x;}
    return out;
}
inline std::optional<nodes::Request> request(std::string text) {
    auto i=text.find("\nnodes2=");if(i==std::string::npos) return std::nullopt;
    text.erase(i+6,1);return nodes::request(text);
}
struct Bone {
    nodes::Transform local,world;
    std::int16_t parent,child;
    std::uint16_t child_count;
    std::int16_t sibling;
    Address node,name;
    std::uint8_t deferred_attach;
    std::uint8_t padding[7];
};
static_assert(sizeof(Bone)==0xa0 && offsetof(Bone,parent)==0x80);
static_assert(offsetof(Bone,node)==0x88 && offsetof(Bone,name)==0x90 && offsetof(Bone,deferred_attach)==0x98);
template<class Reader> Json flattened(Reader& reader,Address self,std::size_t maximum=2048) {
    using nodes::field;Json::Array rows,errors;std::vector<std::int16_t> parents;
    auto count=field<std::int32_t>(reader,self,0x140), expanded=field<std::int32_t>(reader,self,0x144);
    auto data=field<Address>(reader,self,0x148);
    bool valid=count && expanded && data && *count>=0 && *expanded>=0 && std::uint32_t(*count)<=maximum &&
        std::uint32_t(*expanded)<=2048 && (!*count || *data);
    if(!valid) errors.emplace_back("invalid_or_unreadable_flattened_array");
    if(valid) for(std::size_t i=0;i<std::uint32_t(*count);++i) {
        auto bone=field<Bone>(reader,*data,i*sizeof(Bone));
        if(!bone) {errors.emplace_back("unreadable_flattened_record_"+std::to_string(i));break;}
        auto name=nodes::fixed_name(reader,bone->name);
        bool indices=bone->parent>=-1 && bone->parent<*count && bone->child>=-1 && bone->child<*count &&
            bone->sibling>=-1 && bone->sibling<*count && bone->child_count<=*count && bone->parent!=std::int32_t(i);
        if(!name || !indices || bone->deferred_attach>1) errors.emplace_back("invalid_flattened_record_"+std::to_string(i));
        parents.push_back(bone->parent);
        rows.push_back(Json::object({{"index",i},{"parent_index",bone->parent},{"child_index",bone->child},
            {"child_count",bone->child_count},{"sibling_index",bone->sibling},{"node_address",hex(bone->node,16)},
            {"name",name?Json(*name):Json()},{"deferred_attach_raw",bone->deferred_attach},
            {"local",nodes::transform(bone->local)},{"world",nodes::transform(bone->world)}}));
    }
    for(std::size_t i=0;i<parents.size();++i) {
        std::set<std::int16_t> seen;std::int16_t p=std::int16_t(i);
        while(p>=0 && std::size_t(p)<parents.size()) {
            if(!seen.insert(p).second) {errors.emplace_back("flattened_parent_cycle");break;}
            p=parents[std::size_t(p)];
        }
        if(!errors.empty()) break;
    }
    return Json::object({{"address",hex(self,16)},{"layout_source","CommonLibF4 AV e337150"},
        {"validation_status","candidate_requires_four_external_proofs"},{"complete",valid && errors.empty()},
        {"bone_count",count?Json(*count):Json()},{"bone_count_expanded",expanded?Json(*expanded):Json()},
        {"array_address",data?Json(hex(*data,16)):Json()},{"maximum",maximum},{"bones",rows},{"errors",errors}});
}
template<class Reader> Json flattened_trees(Reader& reader,const Json& tree) {
    Json::Array out;std::size_t remaining=4096;
    auto rows=member(tree,"nodes");auto array=rows?std::get_if<Json::Array>(&rows->data):nullptr;
    if(array) for(const auto& n:*array) if(string(member(n,"type"))==std::optional<std::string>("BSFlattenedBoneTree")) {
        auto a=address(member(n,"address"));if(!a) continue;
        if(out.size()>=8 || !remaining) {
            out.push_back(Json::object({{"address",hex(*a,16)},{"complete",false},
                {"validation_status","candidate_requires_four_external_proofs"},
                {"bone_count",nullptr},{"bone_count_expanded",nullptr},{"bones",Json::Array{}},
                {"errors",Json::Array{Json("flattened_aggregate_limit")}}}));break;
        }
        auto candidate=flattened(reader,*a,std::min<std::size_t>(2048,remaining));
        auto bones=member(candidate,"bones");remaining-=std::get<Json::Array>(bones->data).size();
        out.push_back(std::move(candidate));
    }
    return out;
}
template<class Reader,class Calls> Json camera(Reader& reader,Calls& calls,Address image) {
    using nodes::field;Json::Array errors;
    auto singleton=field<Address>(reader,image,camera_rva);
    auto root=singleton?field<Address>(reader,*singleton,0x20):std::nullopt;
    auto state=singleton?field<Address>(reader,*singleton,0x28):std::nullopt;
    auto id=state?field<std::uint32_t>(reader,*state,0x20):std::nullopt;
    auto world_fov=singleton?field<std::uint32_t>(reader,*singleton,0x168):std::nullopt;
    auto first_fov=singleton?field<std::uint32_t>(reader,*singleton,0x16c):std::nullopt;
    auto tree=nodes::tree(reader,calls,root.value_or(0));std::vector<Address> cameras;
    auto rs=member(tree,"nodes");auto rows=rs?std::get_if<Json::Array>(&rs->data):nullptr;
    if(rows) for(const auto& n:*rows) if(string(member(n,"type"))==std::optional<std::string>("NiCamera")) {
        auto a=address(member(n,"address"));if(a) cameras.push_back(*a);
    }
    Json observed;
    if(cameras.size()!=1) errors.emplace_back("active_camera_root_requires_exactly_one_NiCamera");
    else {
        const Address cam=cameras[0];auto obj=read_at<Reader,nodes::Object>(reader,cam);
        auto matrix=field<std::array<std::uint32_t,16>>(reader,cam,0x120);
        auto frustum=field<std::array<std::uint32_t,6>>(reader,cam,0x160);
        auto ortho=field<std::uint8_t>(reader,cam,0x178);
        auto port=field<std::array<std::uint32_t,4>>(reader,cam,0x184);
        if(!obj || !matrix || !frustum || !ortho || *ortho>1 || !port) errors.emplace_back("camera_fields_unreadable");
        else {
            Json::Array m,f,p;
            for(auto v:*matrix) m.push_back(nodes::scalar(v));
            for(auto v:*frustum) f.push_back(nodes::scalar(v));
            for(auto v:*port) p.push_back(nodes::scalar(v));
            observed=Json::object({{"address",hex(cam,16)},{"world",nodes::transform(obj->world)},
                {"world_to_camera_raw",m},{"frustum_left_right_top_bottom_near_far",f},
                {"orthographic",bool(*ortho)},{"viewport_left_right_top_bottom",p}});
        }
    }
    if(!singleton || !*singleton || !root || !*root || !id || !world_fov || !first_fov) errors.emplace_back("player_camera_fields_unavailable");
    auto hierarchy=member(tree,"transforms_and_hierarchy_complete");
    auto readable=hierarchy?std::get_if<bool>(&hierarchy->data):nullptr;
    if(!readable || !*readable) errors.emplace_back("camera_hierarchy_incomplete");
    return Json::object({{"complete",errors.empty()},{"selection","unique NiCamera under SDK PlayerCamera.cameraNode"},
        {"singleton_address",singleton?Json(hex(*singleton,16)):Json()},{"state_id",id?Json(*id):Json()},
        {"world_fov_raw",world_fov?nodes::scalar(*world_fov):Json()},
        {"first_person_fov_raw",first_fov?nodes::scalar(*first_fov):Json()},
        {"camera",observed},{"root_tree",tree},{"errors",errors}});
}
}
