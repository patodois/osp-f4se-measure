// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include "osp/observe.hpp"
#include <set>

namespace osp::weapon {
using layout::Address;
// Independent byte views. All offsets and their derivations: docs/provenance.md.
struct Instance {
    std::byte prefix[0x68]; Address ammo;
    std::byte pad70[0x88-0x70]; Address aim, zoom;
    std::byte pad98[0xc4-0x98];
    float reload_speed,speed,reach,min_range,max_range,attack_delay,unknown_dc,out_of_range;
    std::byte pade4[0x110-0xe4]; std::uint32_t flags;
    std::byte pad114[0x130-0x114];
    std::uint16_t capacity,damage,unknown134; std::uint8_t accuracy_bonus,unknown137;
};
struct Equip { Address item,instance,slot,unknown,equipped; };
struct Slot { Address item,instance,extra; std::byte tail[0x58-0x18]; };
struct InventoryItem { Address form,stack; };
struct Stack { std::byte prefix[0x10]; Address next,extra; std::int32_t count; std::uint16_t flags,pad; };
struct Extra { Address vtable,next; std::uint16_t unknown10; std::uint8_t type,unknown13; std::uint32_t unknown14; Address data; };
struct ModBlock { Address forms; std::uint32_t bytes,pad; };
static_assert(sizeof(Instance)==0x138 && offsetof(Instance,ammo)==0x68);
static_assert(offsetof(Instance,aim)==0x88 && offsetof(Instance,zoom)==0x90);
static_assert(offsetof(Instance,reload_speed)==0xc4 && offsetof(Instance,attack_delay)==0xd8);
static_assert(offsetof(Instance,out_of_range)==0xe0 && offsetof(Instance,flags)==0x110);
static_assert(offsetof(Instance,capacity)==0x130 && offsetof(Instance,damage)==0x132 && offsetof(Instance,accuracy_bonus)==0x136);
static_assert(sizeof(Equip)==0x28 && sizeof(Slot)==0x58 && sizeof(Stack)==0x28);
static_assert(offsetof(Extra,type)==0x12 && offsetof(Extra,data)==0x18 && offsetof(ModBlock,bytes)==8);
template<class R,class T> std::optional<T> at(R& r,Address p,std::size_t off=0) {
    if(!p || off>UINT64_MAX-p) return std::nullopt;
    return read_at<R,T>(r,p+off);
}
template<class R> Json observe(R& r,Address player,Json::Array& errors) {
    Json::Array local;
    auto fail=[&](const std::string& s){local.emplace_back(s);errors.emplace_back("equipped_weapon: "+s);};
    auto form_id=[&](Address p)->Json { if(!p)return Json();auto v=at<R,layout::Form>(r,p);if(!v){fail("unreadable referenced form");return Json();}return hex(v->id); };
    auto process=at<R,Address>(r,player,0x300);
    auto data=process?at<R,Address>(r,*process,8):std::nullopt;
    auto arr=data?at<R,layout::Array>(r,*data,0x288):std::nullopt;
    std::vector<Equip> found;
    bool inventory_ok=true;
    if(!arr || arr->count>64 || arr->count>arr->capacity || arr->capacity>1024 ||
       (arr->count && !address_range(arr->entries,std::size_t(arr->count)*sizeof(Equip)))) {
        fail("equipment process array unavailable or invalid");inventory_ok=false;
    } else for(std::uint32_t i=0;i<arr->count;++i) {
        auto e=at<R,Equip>(r,arr->entries,std::size_t(i)*sizeof(Equip));
        if(!e){fail("unreadable equipment entry");inventory_ok=false;continue;}
        if(!e->item)continue;
        auto f=at<R,layout::Form>(r,e->item);
        if(!f){fail("unreadable equipment form");inventory_ok=false;continue;}
        if(f->type==43)found.push_back(*e);
    }
    if(!inventory_ok || found.size()!=1) {
        if(inventory_ok && found.empty()) return Json::object({{"complete",true},{"status","none"},{"weapon_count",0}});
        fail("exactly one equipped weapon could not be established");
        return Json::object({{"complete",false},{"status","unavailable_or_ambiguous"},{"weapon_count",found.size()},{"errors",local}});
    }
    const auto e=found.front();auto inst=at<R,Instance>(r,e.instance);
    Json::Object fields;
    bool instance_ok=bool(inst);
    if(!inst) fail("instance data null or unreadable; base data is not substituted");
    if(inst) {
        for(auto pair:{std::pair<const char*,float>{"min_range",inst->min_range},{"max_range",inst->max_range},
            {"attack_delay",inst->attack_delay},{"speed",inst->speed},{"reload_speed",inst->reload_speed},
            {"out_of_range_multiplier",inst->out_of_range}}) {
            if(!std::isfinite(pair.second)){fail(std::string("non-finite ")+pair.first);instance_ok=false;}
            fields.emplace_back(pair.first,std::isfinite(pair.second)?Json(double(pair.second)):Json());
        }
        fields.emplace_back("capacity",inst->capacity);fields.emplace_back("accuracy_bonus",inst->accuracy_bonus);
        fields.emplace_back("base_damage",inst->damage);fields.emplace_back("flags_hex",hex(inst->flags));
        fields.emplace_back("automatic_sdk_flag",bool(inst->flags&0x4000));fields.emplace_back("bolt_action_sdk_flag",bool(inst->flags&0x400000));
        fields.emplace_back("flag_interpretation","SDK mask tests only; runtime semantics not independently verified");
        auto ammo_id=form_id(inst->ammo);
        if(inst->ammo && std::holds_alternative<std::nullptr_t>(ammo_id.data))instance_ok=false;
        fields.emplace_back("ammo_form_id",ammo_id);
    }
    // Prefer the named equipment SlotData extra pointer, with matching identity.
    std::set<Address> mod_objects;Json::Array views,inventory_trace;bool mods_coverage_ok=true;
    for(auto offset:{std::size_t(0x428),std::size_t(0xb70)}) {
        auto p=at<R,Address>(r,player,offset);std::size_t items=0,instances=0,extras=0;bool slots_ok=true;
        if(!p || !*p){views.push_back(Json::object({{"actor_offset_hex",hex(offset)},{"available",false}}));continue;}
        for(std::size_t i=0;i<44;++i) {
            auto s=at<R,Slot>(r,*p,0x10+i*sizeof(Slot));
            if(!s)slots_ok=false;
            if(s && s->item==e.item)++items;
            if(s && s->item==e.item && s->instance==e.instance)++instances;
            if(s && s->item==e.item && s->instance==e.instance && s->extra)++extras;
            if(s && s->item==e.item && s->instance==e.instance && s->extra)mod_objects.insert(s->extra);
        }
        if(!slots_ok){fail("unreadable equipment slot view");mods_coverage_ok=false;}
        views.push_back(Json::object({{"actor_offset_hex",hex(offset)},{"available",true},
            {"matching_items",items},{"matching_instances",instances},{"non_null_extras",extras}}));
    }
    // Read-only equivalent of the SDK's equipped-stack traversal; bounded/cycle checked.
    if(mod_objects.empty()) {
        auto p=at<R,Address>(r,player,0xf8);auto a=p?at<R,layout::Array>(r,*p,0x58):std::nullopt;
        if(a && a->count<=a->capacity && a->capacity<=100000 && (!a->count || address_range(a->entries,std::size_t(a->count)*16))) {
            inventory_trace.push_back(Json::object({{"items_count",a->count}}));
            for(std::uint32_t i=0;i<a->count;++i) {
                auto item=at<R,InventoryItem>(r,a->entries,std::size_t(i)*16);
                if(!item){fail("unreadable inventory item");mods_coverage_ok=false;continue;}
                if(item->form!=e.item)continue;
                std::set<Address> seen;
                for(Address cur=item->stack;cur;) {
                    if(seen.size()>=1024 || !seen.insert(cur).second){fail("inventory stack cycle or cap");mods_coverage_ok=false;break;}
                    auto st=at<R,Stack>(r,cur);if(!st){fail("unreadable inventory stack");mods_coverage_ok=false;break;}
                    inventory_trace.push_back(Json::object({{"matching_item_index",i},{"stack_flags_hex",hex(st->flags)},
                        {"extra_list_present",bool(st->extra)}}));
                    if(st->flags&7) {
                        auto head=at<R,Address>(r,st->extra,8);std::set<Address> extras;
                        if(!head){fail("equipped extra-data list null or unreadable");mods_coverage_ok=false;}
                        for(Address x=head.value_or(0);x;) {
                            if(extras.size()>=1024 || !extras.insert(x).second){fail("extra-data cycle or cap");mods_coverage_ok=false;break;}
                            auto v=at<R,Extra>(r,x);if(!v){fail("unreadable extra-data node");mods_coverage_ok=false;break;}
                            inventory_trace.push_back(Json::object({{"extra_type_hex",hex(v->type)}}));
                            if(v->type==0x35)mod_objects.insert(x);
                            x=v->next;
                        }
                    }
                    cur=st->next;
                }
            }
        }else {fail("inventory mod fallback array unavailable or invalid");mods_coverage_ok=false;}
    }
    Json::Array mods;bool mods_ok=!mod_objects.empty() && mods_coverage_ok;std::optional<std::vector<std::uint32_t>> agreed;
    if(!mods_ok)fail("object-instance mod list unavailable or incompletely traversed");
    for(Address object:mod_objects) {
        auto x=at<R,Extra>(r,object);auto b=x&&x->type==0x35?at<R,ModBlock>(r,x->data):std::nullopt;
        if(!b || b->bytes%8 || b->bytes>65536 || (b->bytes && !address_range(b->forms,b->bytes))) {
            fail("invalid object-instance mod block");mods_ok=false;
        } else {
            std::vector<std::uint32_t> ids;
            for(std::size_t i=0;i<b->bytes/8;++i) {
                auto id=at<R,std::uint32_t>(r,b->forms,i*8);
                if(!id){fail("unreadable object-instance mod FormID");mods_ok=false;break;}
                ids.push_back(*id);
            }
            if(agreed && *agreed!=ids){fail("equipment views disagree about object-instance mods");mods_ok=false;}
            else agreed=ids;
        }
    }
    if(agreed)for(auto id:*agreed)mods.emplace_back(hex(id));
    Json aim_id=inst?form_id(inst->aim):Json(),zoom_id=inst?form_id(inst->zoom):Json();
    return Json::object({{"complete",false},{"status","observed_with_published_layout_limits"},
        {"weapon_form_id",form_id(e.item)},{"instance_address_hex",hex(e.instance,16)},
        {"equipment_views",views},{"inventory_mod_trace",inventory_trace},
        {"instance_complete",instance_ok},{"fields",Json::object(fields)},
        {"aim",Json::object({{"form_id",aim_id},{"complete",false},{"fields",nullptr},
            {"reason","SDK only forward-declares BGSAimModel; resolve published record fields externally"}})},
        {"zoom",Json::object({{"form_id",zoom_id},{"present",inst?Json(bool(inst->zoom)):Json()},
            {"complete",bool(inst && !inst->zoom)},{"fov",nullptr},
            {"reason","SDK only forward-declares BGSZoomData; no guessed memory layout"}})},
        {"mods",Json::object({{"complete",mods_ok},{"candidate_objects",mod_objects.size()},
            {"form_ids",mods},{"source","BGSObjectInstanceExtra.Data.forms"}})},
        {"errors",local}});
}
}
