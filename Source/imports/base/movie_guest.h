#pragma once

struct Cpu;

/* Returns nonzero when the Catherine H.264 decoder handled this dispatch. */
int catherine_movie_guest_dispatch(struct Cpu *cpu);
