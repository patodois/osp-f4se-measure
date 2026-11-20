// SPDX-License-Identifier: MIT OR Apache-2.0
// Standalone owner-run test; does not load F4SE or the game.
#include <cassert>
#include <cstdint>
#include <cstdio>
extern "C" bool osp_call_float(std::uintptr_t,std::uintptr_t,std::uintptr_t,float*);
extern "C" bool osp_call_word(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uint64_t*);
extern "C" bool osp_call_void2(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
extern "C" __attribute__((noinline)) float number(void* self,void* arg) {
    return *static_cast<float*>(self)+*static_cast<float*>(arg);
}
extern "C" __attribute__((noinline)) std::uint64_t word(void* self,void*) {
    return *static_cast<std::uint64_t*>(self);
}
extern "C" __attribute__((noinline)) void two_args(void* self,void* first,void* second) {
    *static_cast<std::uint64_t*>(self)=*static_cast<std::uint64_t*>(first)+*static_cast<std::uint64_t*>(second);
}
int main() {
    float a=1.25f,b=2.5f,out=0;
    std::uint64_t input=0x123456789abcdef0,output=0;
    auto address=[](auto p) { return reinterpret_cast<std::uintptr_t>(p); };
    assert(osp_call_float(address(number),address(&a),address(&b),&out) && out==3.75f);
    assert(osp_call_word(address(word),address(&input),0,&output) && output==input);
    assert(!osp_call_float(0,address(&a),address(&b),&out));
    assert(!osp_call_float(address(number),0,address(&b),&out));
    assert(!osp_call_float(address(number),address(&a),address(&b),nullptr));
    // An unreadable self pointer tests a fault inside the getter, not a null gate.
    assert(!osp_call_float(address(number),1,address(&b),&out));
    assert(!osp_call_word(address(word),1,0,&output));
    std::uint64_t one=3,two=7;
    assert(osp_call_void2(address(two_args),address(&output),address(&one),address(&two)) && output==10);
    assert(!osp_call_void2(address(two_args),1,address(&one),address(&two)));
    assert(osp_call_float(address(number),address(&a),address(&b),&out) && out==3.75f);
    std::puts("PASS: Microsoft x64 calls, null guards, hardware-fault recovery");
}
