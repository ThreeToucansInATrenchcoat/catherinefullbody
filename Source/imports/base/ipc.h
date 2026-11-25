#pragma once
#include "cpu.h"
int ipc_svc(Cpu *cpu, uint32_t immediate);
HANDLE ipc_event(uint32_t handle);
