// Module layout chosen by recomp/layout.py. Generated; do not edit.
#pragma once

#define MOD_MAIN_BASE 0x7000000000ull
#define MOD_MAIN_SIZE 0x3cce000ull
#define MOD_SUBSDK0_BASE 0x7003e00000ull
#define MOD_SUBSDK0_SIZE 0x944000ull
#define MOD_SUBSDK1_BASE 0x7004800000ull
#define MOD_SUBSDK1_SIZE 0x715000ull
#define MOD_SDK_BASE 0x7005000000ull
#define MOD_SDK_SIZE 0xcfb000ull

#define MOD_COUNT 4
#define MOD_LIST(X) X(main, MAIN) X(subsdk0, SUBSDK0) X(subsdk1, SUBSDK1) X(sdk, SDK)
