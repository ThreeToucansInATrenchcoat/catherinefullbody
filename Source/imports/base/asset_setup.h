#pragma once
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
int catherine_asset_setup(const wchar_t* pack_path);
int catherine_translate_shader(const char* hash,const uint8_t* code,size_t size,const uint8_t* control,void** binary,size_t* length,char** resources);
#ifdef __cplusplus
}
#endif
