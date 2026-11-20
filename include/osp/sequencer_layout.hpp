// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
// Plugin v9 additions: independent byte views and addresses from the pinned
// F4SE 0.6.23 package. Sources and use: docs/provenance.md, "Version 9".
#include "osp/layouts.hpp"

namespace osp::layout9 {
using layout::Address;
// GameAPI.cpp:17, a RelocPtr<ConsoleManager*>: the RVA holds the manager pointer.
constexpr Address console_manager_rva=0x058E0AE0;
// GameAPI.h:23, ConsoleManager::Print(const char*), member call with explicit this.
constexpr Address console_print_rva=0x01262F50;
// GameSettings.cpp:5,7, RelocPtr<INISettingCollection*> / <INIPrefSettingCollection*>.
constexpr Address ini_settings_rva=0x05EDB528;
constexpr Address ini_pref_settings_rva=0x05B5BE58;
// GameSettings.h:96-110: SettingCollectionList::data (Node*) at +0x120;
// Node {Setting* data; Node* next}. GameSettings.cpp:119-137 walks it until null.
constexpr std::size_t setting_list_head=0x120;
struct SettingNode { Address data, next; };
// GameData.h:134 arrGLOB is the form array of form type 11 (GameForms.h:215-226).
constexpr std::size_t glob_array_index=11;
constexpr std::uint8_t glob_form_type=11;
// GameForms.h:1754-1763 TESGlobal: const char* editorID +0x20, float value +0x30.
constexpr std::size_t global_editor_id=0x20, global_value=0x30;
// GameReferences.h:209-212 TESObjectREFR parentCell +0xB8, pos +0xD0 (three floats).
constexpr std::size_t reference_parent_cell=0xb8, reference_position=0xd0;
static_assert(sizeof(SettingNode)==0x10 && offsetof(SettingNode,next)==8);
static_assert(offsetof(layout::DataHandler,forms)+glob_array_index*sizeof(layout::Array)==0x170);
}
