#version 450
#extension GL_GOOGLE_include_directive : require
// Catherine presentation and FXAA. FXAA is an edge-directed, single-pass LDR
// filter following Timothy Lottes' algorithm description:
// https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
// This implementation uses a bounded eight-texel span and contrast rejection.
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 2) uniform sampler2D dev_ui;
layout(set = 0, binding = 1) uniform Overlay { vec4 shape[32]; } ov;
layout(push_constant) uniform P {
    vec4 rot;     // mat2 columns
    vec4 off;     // rotation offset, overlay count, upright target aspect ratio
    vec4 box;     // upright destination rectangle
    vec4 crop;    // source rectangle in normalized texture coordinates
    vec4 aa;  // source texel width/height, FXAA enabled, reserved
    vec4 fsr; // 0 native / 1 EASU / 2 RCAS, sharpness stops, output width/height
} p;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

#define A_GPU 1
#define A_GLSL 1
#include "fsr1/ffx_a.h"
#define FSR_EASU_F 1
#define FSR_RCAS_F 1
AF4 FsrEasuRF(AF2 at){return textureGather(src,at,0);}
AF4 FsrEasuGF(AF2 at){return textureGather(src,at,1);}
AF4 FsrEasuBF(AF2 at){return textureGather(src,at,2);}
AF4 FsrRcasLoadF(ASU2 at){return texelFetch(src,clamp(at,ivec2(0),textureSize(src,0)-1),0);}
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b){}
#include "fsr1/ffx_fsr1.h"

vec3 sample_picture(vec2 at) {
    // Filtering must not read pixels outside an NVN window crop.
    vec2 half_texel = 0.5 * p.aa.xy;
    return textureLod(src, clamp(at, p.crop.xy + half_texel,
                                p.crop.xy + p.crop.zw - half_texel), 0.0).rgb;
}
float luma(vec3 rgb) { return dot(rgb, vec3(0.299, 0.587, 0.114)); }
vec3 fxaa(vec2 at) {
    vec2 pixel = p.aa.xy;
    vec3 center = sample_picture(at);
    float nw = luma(sample_picture(at + vec2(-1.0, -1.0) * pixel));
    float ne = luma(sample_picture(at + vec2( 1.0, -1.0) * pixel));
    float sw = luma(sample_picture(at + vec2(-1.0,  1.0) * pixel));
    float se = luma(sample_picture(at + vec2( 1.0,  1.0) * pixel));
    float middle = luma(center);
    float low = min(middle, min(min(nw, ne), min(sw, se)));
    float high = max(middle, max(max(nw, ne), max(sw, se)));
    if (high - low < max(1.0 / 32.0, high * (1.0 / 8.0))) return center;

    vec2 direction = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduction = max((nw + ne + sw + se) * (1.0 / 32.0), 1.0 / 128.0);
    direction *= 1.0 / (min(abs(direction.x), abs(direction.y)) + reduction);
    direction = clamp(direction, vec2(-8.0), vec2(8.0)) * pixel;
    vec3 inner = 0.5 * (sample_picture(at - direction * (1.0 / 6.0)) +
                        sample_picture(at + direction * (1.0 / 6.0)));
    vec3 outer = inner * 0.5 + 0.25 * (sample_picture(at - direction * 0.5) +
                                      sample_picture(at + direction * 0.5));
    float filtered = luma(outer);
    return filtered < low || filtered > high ? inner : outer;
}
void main() {
    if(p.fsr.x>0.5 && p.fsr.x<1.5){
        uvec4 c0,c1,c2,c3;vec2 size=vec2(textureSize(src,0));
        FsrEasuConOffset(c0,c1,c2,c3,p.crop.z*size.x,p.crop.w*size.y,size.x,size.y,p.fsr.z,p.fsr.w,p.crop.x*size.x,p.crop.y*size.y);
        vec3 result;FsrEasuF(result,uvec2(gl_FragCoord.xy),c0,c1,c2,c3);
        color=vec4(result,1.0);return;
    }
    vec2 up = mat2(p.rot.xy, p.rot.zw) * uv + p.off.xy;
    vec2 b = (up - p.box.xy) / p.box.zw;
    vec3 c = vec3(0.0);
    if (all(greaterThanEqual(b, vec2(0.0))) && all(lessThanEqual(b, vec2(1.0)))) {
        vec2 at = p.crop.xy + b * p.crop.zw;
        if(p.fsr.x>1.5 && p.fsr.y>=0.0){
            uvec4 config;FsrRcasCon(config,p.fsr.y);
            FsrRcasF(c.r,c.g,c.b,uvec2(clamp(at,vec2(0.0),vec2(1.0))*vec2(textureSize(src,0))),config);
        }else c = p.aa.z > 0.5 ? fxaa(at) : texture(src, at).rgb;
    }
    // Host controls remain sharp: compose them after filtering the game image.
    for (int i = 0; i < int(p.off.z); i++) {
        vec4 s = ov.shape[i];
        float d = length(vec2((up.x - s.x) * p.off.w, up.y - s.y));
        if (d < s.z) c = mix(c, vec3(1.0), s.w);
        else if (d < s.z + 0.005) c = mix(c, vec3(1.0), min(1.0, s.w * 3.0));
    }
    if (p.aa.w > 0.5 && all(greaterThanEqual(b, vec2(0.0))) && all(lessThanEqual(b, vec2(1.0)))) {
        vec4 menu = texture(dev_ui, b);
        c = menu.rgb + c * (1.0-menu.a);
    }
    color = vec4(c, 1.0);
}
