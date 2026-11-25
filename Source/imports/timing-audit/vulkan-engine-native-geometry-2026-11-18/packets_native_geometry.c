// hook90: a native replacement for the engine's render-packet interpreter f_70002fb1d0 (main_0022.c), the first of
// the direct game hooks. The original (recompiled AArch64):
//     sp -= 0x30; [sp+32] = {x29, x30}; x29 = sp + 0x20; [sp+0x10] = x0 (the list cursor);
//     for (fn = *[x0]; fn; fn = *[[sp+0x10]]) { x0 = sp + 8; call fn }   // each packet advances [sp+0x10]
//     restore x29, x30; sp += 0x30
// This keeps the same guest frame and register effects (x0 as the last packet left it, x8 = 0, x29, x30, sp) and
// calls the engine's packet functions directly instead of through rt_dispatch (whose special cases â€” SDK, movie,
// loader, HLE and export addresses â€” never match these game functions, so the same function runs).
// Census (CATHERINE_PARALLEL90_CENSUS=1): packet functions, sizes and runs, written once to packets90.txt.
// CATHERINE_PARALLEL90=0: every packet through rt_dispatch, as before.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

void f_70002fc1d0(Cpu *c);
void catherine_engine_indexed_packet(Cpu *c);  // draw
void f_70002fb3a0(Cpu *c);  // model matrix
void f_70002fb2d0(Cpu *c);  // material
void f_70002fb210(Cpu *c);  // material with skin palette

static int setting(const char *name, int fallback) {
    const char *e = getenv(name);
    return e && *e ? atoi(e) : fallback;
}

void f_70002fc100(Cpu *c);
void catherine_engine_stream_packet(Cpu *c);
void f_70002fc220(Cpu *c);
void f_70002fc050(Cpu *c);
void f_70002fb790(Cpu *c);
void f_70002fc2f0(Cpu *c);
void f_70002fc270(Cpu *c);
void f_70002fc330(Cpu *c);
void f_70002fb6b0(Cpu *c);
void catherine_engine_compute_packet(Cpu *c);
void f_70002fc180(Cpu *c);
void catherine_engine_array_packet(Cpu *c);
void f_70002fc3c0(Cpu *c);
void f_70002fb3e0(Cpu *c);
void f_70002fc480(Cpu *c);
void f_70002fc4c0(Cpu *c);
void catherine_engine_mesh_packet(Cpu *c);

static int expanded_mode(void) {
    return setting("CATHERINE_DIRECT_GRAPHICS", 1);
}

// ---- census ------------------------------------------------------------------------------------------------------
#define CENSUS_FNS 256
static struct { uint64_t fn, count, bytes; } g_fns[CENSUS_FNS];
static uint64_t g_packets, g_draws, g_draw_same, g_draw_same_between, g_runs[8], g_run_len;
static uint8_t g_last_draw[0x28], g_between[512];
static uint32_t g_between_len, g_last_between_len;
static uint8_t g_last_between[512];
static int g_census_done;

static void census_fn(uint64_t fn, uint64_t size) {
    for (int i = 0; i < CENSUS_FNS; i++) {
        if (g_fns[i].fn == fn || !g_fns[i].fn) {
            g_fns[i].fn = fn, g_fns[i].count++, g_fns[i].bytes += size;
            return;
        }
    }
}

// Between two draw packets: the packets other than the draw, with each model-matrix packet's pointer blanked, so
// "same between" means the same packets except for which matrix they point at.
static void census_packet(uint64_t fn, const uint8_t *p, uint64_t size) {
    ++g_packets;
    census_fn(fn, size);
    if (fn == 0x70002fc1d0ull) {
        ++g_draws;
        const int same = size == 0x28 && memcmp(p, g_last_draw, 0x28) == 0;
        const int same_between = g_between_len == g_last_between_len && memcmp(g_between, g_last_between, g_between_len) == 0;
        g_draw_same += same;
        g_draw_same_between += same && same_between;
        if (same && same_between) g_run_len++;
        else {
            g_runs[g_run_len >= 7 ? 7 : g_run_len]++;
            g_run_len = 0;
        }
        if (size == 0x28) memcpy(g_last_draw, p, 0x28);
        memcpy(g_last_between, g_between, g_between_len);
        g_last_between_len = g_between_len;
        g_between_len = 0;
        return;
    }
    if (g_between_len + size <= sizeof g_between) {
        memcpy(g_between + g_between_len, p, size);
        if (fn == 0x70002fb3a0ull && size >= 16) memset(g_between + g_between_len + 8, 0, 8);  // the matrix pointer
        g_between_len += (uint32_t)size;
    } else {
        g_between_len = sizeof g_between + 1;  // too long to compare: never "same"
    }
}

static void census_dump(void) {
    FILE *f = fopen("packets90.txt", "w");
    if (!f) return;
    fprintf(f, "packets %llu, draws %llu, draw packets identical to the previous %llu, identical with identical packets "
               "between (matrix pointer aside) %llu\n",
            (unsigned long long)g_packets, (unsigned long long)g_draws, (unsigned long long)g_draw_same,
            (unsigned long long)g_draw_same_between);
    fprintf(f, "runs ending (extra repeats 0..6, 7+):");
    for (int i = 0; i < 8; i++) fprintf(f, " %llu", (unsigned long long)g_runs[i]);
    fprintf(f, "\nfunction count avg_bytes\n");
    for (int i = 0; i < CENSUS_FNS && g_fns[i].fn; i++)
        fprintf(f, "%llx %llu %.1f\n", (unsigned long long)g_fns[i].fn, (unsigned long long)g_fns[i].count,
                (double)g_fns[i].bytes / (double)g_fns[i].count);
    fclose(f);
}

// Raw dump (CATHERINE_PARALLEL90_CENSUS=2): 8000 packets from the 200,000th model-matrix packet on, one line each:
// function, size, first 96 bytes in hex (and a hash of the whole packet), to packets90-raw.txt.
static void dump_packet(uint64_t fn, const uint8_t *p, uint64_t size) {
    static uint64_t n, matrices, start = ~0ull;
    static FILE *f;
    const uint64_t k = n++;
    if (fn == 0x70002fb3a0ull && ++matrices == 200000) start = k;  // well inside the tower scene
    if (k < start || k >= start + 8000) {
        if (start != ~0ull && k == start + 8000 && f) fclose(f), f = NULL;
        return;
    }
    if (!f && !(f = fopen("packets90-raw.txt", "w"))) return;
    uint64_t h = 1469598103934665603ull;
    for (uint64_t i = 0; i < size && i < 4096; i++) h = (h ^ p[i]) * 1099511628211ull;
    fprintf(f, "%llu %llx %llu %016llx ", (unsigned long long)k, (unsigned long long)fn, (unsigned long long)size, (unsigned long long)h);
    for (uint64_t i = 0; i < size && i < 96; i++) fprintf(f, "%02x", p[i]);
    fputc(10, f);
}

// ---- the interpreter ---------------------------------------------------------------------------------------------
static void call_packet(Cpu *c, uint64_t fn, int direct) {
    if (direct & 1) {
        switch (fn) {
            case 0x70002fc1d0ull: catherine_engine_indexed_packet(c); return;
            case 0x70002fb3a0ull: f_70002fb3a0(c); return;
            case 0x70002fb2d0ull: f_70002fb2d0(c); return;
            case 0x70002fb210ull: f_70002fb210(c); return;
            default: break;
        }
    }
    if (direct & 2) {
        switch (fn) {
            case 0x70002fc100ull: c->pc = fn; catherine_engine_stream_packet(c); return;
            case 0x70002fc220ull: c->pc = fn; f_70002fc220(c); return;
            case 0x70002fc050ull: c->pc = fn; f_70002fc050(c); return;
            case 0x70002fb790ull: c->pc = fn; f_70002fb790(c); return;
            case 0x70002fc2f0ull: c->pc = fn; f_70002fc2f0(c); return;
            case 0x70002fc270ull: c->pc = fn; f_70002fc270(c); return;
            case 0x70002fc330ull: c->pc = fn; f_70002fc330(c); return;
            case 0x70002fb6b0ull: c->pc = fn; catherine_engine_compute_packet(c); return;
            case 0x70002fc180ull: c->pc = fn; catherine_engine_array_packet(c); return;
            case 0x70002fc3c0ull: c->pc = fn; f_70002fc3c0(c); return;
            case 0x70002fb3e0ull: c->pc = fn; f_70002fb3e0(c); return;
            case 0x70002fc480ull: c->pc = fn; f_70002fc480(c); return;
            case 0x70002fc4c0ull: c->pc = fn; catherine_engine_mesh_packet(c); return;
            default: break;
        }
    }
    c->pc = fn;
    rt_dispatch(c);
}

void catherine_packets90(Cpu *c) {
    static int direct = -1, census = -1;
    if (direct < 0) direct = setting("CATHERINE_PARALLEL90", 1) ? (expanded_mode() ? 3 : 1) : 0, census = setting("CATHERINE_PARALLEL90_CENSUS", 0);
    uint64_t sp = c->sp - 0x30;
    ST64(sp + 32, c->x[29]);
    ST64(sp + 40, c->x[30]);
    const uint64_t x29 = sp + 0x20;
    ST64(sp + 0x10, c->x[0]);
    uint64_t fn = LD64(c->x[0]);
    while (fn) {
        const uint64_t at = LD64(sp + 0x10);
        c->x[0] = sp + 8;
        c->x[8] = fn;
        c->x[29] = x29;
        c->x[30] = 0x70002fb1f0ull;
        c->sp = sp;
        call_packet(c, fn, direct);
        const uint64_t next = LD64(sp + 0x10);
        if (census == 2) dump_packet(fn, (const uint8_t *)(uintptr_t)at, next - at);
        if (census == 1 && !g_census_done) {
            census_packet(fn, (const uint8_t *)(uintptr_t)at, next - at);
            if (g_packets >= 3000000) g_census_done = 1, census_dump();
        }
        fn = LD64(next);
    }
    c->x[8] = fn;
    c->x[29] = LD64(sp + 32);
    c->x[30] = LD64(sp + 40);
    c->sp = sp + 0x30;
}
