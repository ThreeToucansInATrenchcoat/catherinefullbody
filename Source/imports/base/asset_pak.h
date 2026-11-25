#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// 1: validated package; 0: default package absent (legacy loose assets); -1: failed.
// An explicit path or an existing invalid package never falls back to loose assets.
int asset_pak_initialize(void);
// Set the package location during startup, before any other asset access.
int asset_pak_initialize_from(const wchar_t* path);
const char* asset_pak_error(void);
int asset_pak_read(const char* relative_path, void* output, size_t exact_size);
int asset_pak_read_alloc(const char* relative_path, void** output, size_t* size);
int asset_pak_contains(const char* relative_path);
void asset_pak_free(void* allocation);
uint64_t asset_pak_romfs_size(void);
int asset_pak_read_romfs(void* output, uint64_t offset, size_t size);
#ifdef CATHERINE_PAK_TESTING
int asset_pak_initialize_for_test(const wchar_t* path);
#endif
#ifdef __cplusplus
}
#endif
