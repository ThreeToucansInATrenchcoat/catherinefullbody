#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int graphics_dispatch(void *cpu, uint64_t address);
void guest_call_original(void *cpu, uint64_t address);
#ifdef __cplusplus
}
#endif
