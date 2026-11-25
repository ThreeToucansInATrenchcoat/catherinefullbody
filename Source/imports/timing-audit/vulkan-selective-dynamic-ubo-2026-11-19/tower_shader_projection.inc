// Build-generated exact hash/classifier projection of actual batch91 kInst91.
struct Inst91{const char* hash;};
const Inst91 kInst91[]={{"45e7f81feab52bc4"},{"e64e368a3d84ea4f"},{"6d2a2e091313584e"},{"86925243ebd092ca"},{"e16573779a2ed545"},{"126cd0e955677f91"}};
bool block_shader(const ShaderInfo* vs) {
    for (const Inst91& m : kInst91)
        if (vs->hash == m.hash) return true;
    return false;
}
