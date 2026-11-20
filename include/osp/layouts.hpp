// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>

// Independently written byte views, not copies of SDK class definitions.
// Source locations and derivations: docs/provenance.md.
namespace osp::layout {
using Address = std::uint64_t;
constexpr Address data_handler_rva = 0x058CF080; // GameData.cpp
constexpr Address player_rva = 0x05AA4388;       // GameReferences.cpp
constexpr Address actor_base_level_rva = 0x001497F0; // GameFormComponents.h
constexpr Address game_settings_rva = 0x058E1030; // GameSettings.cpp:11
constexpr Address setting_lookup_rva = 0x0001E290; // GameSettings.h:75
constexpr Address fixed_string_ctor_rva = 0x01B41D40; // GameTypes.h:146
constexpr Address fixed_string_release_rva = 0x01B42FD0; // GameTypes.h:153
constexpr Address console_commands_rva = 0x03706DC0; // ObScript.cpp:6
constexpr std::size_t console_command_count = 0x209; // ObScript.h:43
constexpr Address script_commands_rva = 0x036F6DD0; // ObScript.cpp:4
constexpr std::size_t script_command_count = 0x332; // ObScript.h:42
constexpr std::size_t current_slot = 1, base_slot = 3; // ActorValueOwner
constexpr std::size_t maximum_slot = 2; // GameFormComponents.h:150
constexpr std::size_t editor_id_slot = 0x3a;          // TESForm
struct Array {
    Address entries;
    std::uint32_t capacity, reserved0, count, reserved1;
};
struct Form {
    Address vtable, source_files;
    std::uint32_t flags, id;
    std::uint16_t unknown;
    std::uint8_t type, reserved0;
    std::uint32_t reserved1;
};
struct Reference {
    std::byte prefix[0x58];
    Address value_owner_vtable;
    std::byte middle[0xe0 - 0x60];
    Address base_form;
};
struct ActorBase {
    std::byte prefix[0x68];
    std::byte actor_data[0x68];
};
struct Mod {
    std::byte prefix[0x70];
    char name[260];
    std::byte middle[0x334 - 0x174];
    std::uint32_t record_flags;
    std::byte middle2[0x370 - 0x338];
    std::uint8_t index, reserved;
    std::uint16_t light_index;
};
struct DataHandler {
    std::byte prefix[0x68];
    Array forms[159];
    std::byte middle[0xfb0 - 0xf50];
    std::byte mod_list_head[0x10];
    Array full_mods, light_mods;
};
struct Setting {
    Address vtable;
    std::uint64_t bits;
    Address name;
};
struct FixedString { Address entry; }; // GameTypes.h:140-146,185
struct SettingCollection {
    std::byte prefix[0x128];
    Address map; // GameSettings.h:80; map remains opaque
};
struct ConsoleCommand {
    Address long_name, short_name;
    std::uint32_t opcode, reserved0;
    Address help;
    std::uint8_t needs_parent, reserved1;
    std::uint16_t parameter_count;
    std::uint32_t reserved2;
    Address parameters, execute, parse, evaluate;
    std::uint32_t flags, reserved3;
}; // ObScript.h:18-38; no GCC SDK class objects
static_assert(sizeof(Address) == 8 && sizeof(Array) == 0x18);
static_assert(offsetof(Array,count) == 0x10);
static_assert(sizeof(Form) == 0x20 && offsetof(Form,id) == 0x14);
static_assert(offsetof(Form,type) == 0x1a);
static_assert(offsetof(Reference,value_owner_vtable) == 0x58);
static_assert(offsetof(Reference,base_form) == 0xe0);
static_assert(offsetof(ActorBase,actor_data) == 0x68);
static_assert(offsetof(DataHandler,forms) + 98*sizeof(Array) == 0x998);
static_assert(offsetof(DataHandler,full_mods) == 0xfc0);
static_assert(offsetof(DataHandler,light_mods) == 0xfd8);
static_assert(offsetof(Mod,name) == 0x70);
static_assert(offsetof(Mod,record_flags) == 0x334);
static_assert(offsetof(Mod,index) == 0x370 && offsetof(Mod,light_index) == 0x372);
static_assert(sizeof(Setting) == 0x18 && offsetof(Setting,bits) == 8 && offsetof(Setting,name) == 0x10);
static_assert(sizeof(FixedString) == 8 && offsetof(FixedString,entry) == 0);
static_assert(offsetof(SettingCollection,map) == 0x128 && sizeof(SettingCollection) == 0x130);
static_assert(sizeof(ConsoleCommand)==0x50 && offsetof(ConsoleCommand,execute)==0x30);
static_assert(offsetof(ConsoleCommand,parameter_count)==0x22 && offsetof(ConsoleCommand,parse)==0x38);
}
