#include <nx/runtime.h>
int main(){return nx::hle_lookup("nvnBootstrapLoader")?0:1;}
extern "C" void guest_call_original(void*,uint64_t) {}
