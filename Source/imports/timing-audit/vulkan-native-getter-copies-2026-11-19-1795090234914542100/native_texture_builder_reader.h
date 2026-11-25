#pragma once
#include <cstdint>
#include <cstring>
namespace catherine::native_builder {
inline constexpr uint64_t addresses[]={0x7005427760ull,0x7005427770ull,0x7005427780ull,0x7005427790ull,0x70054277a0ull,0x70054277b0ull,0x70054277c0ull,0x70054277d0ull,0x7005427810ull,0x7005427880ull,0x7005427890ull,0x70054278a0ull,0x700541ea10ull,0x700541ea20ull,0x700541ea30ull,0x70054126d0ull,0x70054126e0ull,0x70054126f0ull};
inline constexpr unsigned offsets[]={32,36,40,44,48,52,56,60,80,100,16,24,16,24,32,16,24,32};
inline constexpr unsigned widths[]={4,4,4,4,4,4,4,4,4,4,8,8,8,8,4,8,8,8};
inline constexpr unsigned count=18;
inline uint64_t read(uint64_t builder,unsigned kind){uint64_t result=0;std::memcpy(&result,reinterpret_cast<const void*>(builder+offsets[kind]),widths[kind]);return result;}
}
