int zero_morph_mode(){
    static const int mode=[] {const char* e=std::getenv("CATHERINE_ZERO_INPLACE_MORPH");return e&&(e[0]=='1'||e[0]=='2')&&!e[1]?e[0]-'0':0;}();return mode;
}
void invalidate_morph_identity(Region& region){region.morph_identity_count=0;region.morph_identity_stride=0;}
