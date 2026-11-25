#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "ipc.h"
#include "asset_pak.h"
#include "guest-timing/include/controller_support_policy.h"
extern unsigned graphics_connected_player_count(void);

enum { SM = 2, LOG_MANAGER = 3, LOGGER = 4, APM = 5, APM_SESSION = 6, APPLET = 7, AOC = 8,
       PCTL = 9, PCTL_S = 10, PCTL_A = 11, PCTL_R = 12, ECTX = 13, TIME = 14,
       SETTINGS = 15, FILESYSTEM = 16, NVDRV = 17,
       VI_MANAGER = 18, VI_SYSTEM = 19, VI_USER = 20, VI_APP_DISPLAY = 21,
       VI_BINDER = 22, VI_SYSTEM_DISPLAY = 23, VI_MANAGER_DISPLAY = 24, VI_INDIRECT = 25,
       AUDREN = 26, AUDIO_DEVICE = 27, AUDIO_OUT_MANAGER = 28, AUDIO_OUT = 29,
       HID = 30, HID_RESOURCE = 31, ACCOUNT = 32, VIBRATION = 33, FRIEND = 34,
       NIFM = 35, NIFM_GENERAL = 36, NIFM_REQUEST = 37 };
static uint8_t is_domain[100], session_type[100];
static LONG next_session = 39;
static int nv_initialized;
static HANDLE applet_event, suspended_event, gpu_event, audio_event, output_event, launch_event;
static HANDLE network_state_event, network_completion_event;
static unsigned applet_messages;
static uint32_t controller_styles;
static int controller_active;
static struct {unsigned char *data;uint64_t size;} storage[32];
static unsigned storage_count;
static unsigned library_applet_id;
static CatherineControllerSupportRequest controller_support_request={0,4,0};

static void reply(uint32_t *message, int tipc, uint32_t moved, uint32_t value) {
    memset(message, 0, 256);
    unsigned start = moved ? 4 : 2;
    if (moved) {
        message[1] = 1u << 31;
        message[2] = 1u << 5;
        message[3] = moved;
    }
    unsigned payload = tipc ? start : (start + 3) & ~3u;
    message[1] |= tipc ? 1 : payload - start + 9;
    if (!tipc) {
        message[payload] = 0x4f434653;
        message[payload + 4] = value;
    }
}

static void domain_reply(uint32_t *message, uint32_t result, uint64_t value,
                         unsigned size, uint32_t object, uint32_t copy) {
    memset(message, 0, 256);
    if (copy) {
        message[1] = 1u << 31;
        message[2] = 2;
        message[3] = copy;
    }
    message[1] |= 12 + (size + 3) / 4 + !!object;
    message[4] = !!object;
    message[8] = 0x4f434653;
    message[10] = result;
    memcpy(message + 12, &value, size);
    if (object) message[12 + (size + 3) / 4] = object;
}

typedef struct {
    int16_t year;
    uint8_t month, day, hour, minute, second, padding;
} ClockCalendar;

typedef struct {
    uint32_t weekday, yearday;
    char zone[8];
    uint8_t dst, padding[3];
    int32_t offset;
} ClockCalendarInfo;

typedef struct {
    ClockCalendar calendar;
    ClockCalendarInfo info;
} ClockCalendarResult;

typedef struct {
    int64_t seconds;
    uint8_t source[16];
} ClockSteadyPoint;

typedef struct {
    int64_t offset;
    ClockSteadyPoint steady;
} ClockContext;

typedef struct {
    ClockContext user_context, network_context;
    int64_t user_time, network_time;
    ClockCalendar user_calendar, network_calendar;
    ClockCalendarInfo user_info, network_info;
    ClockSteadyPoint steady;
    char location[0x24];
    uint8_t automatic_correction, type, padding[2];
} ClockSnapshot;

_Static_assert(sizeof(ClockCalendarResult) == 0x20, "time calendar response ABI");
_Static_assert(sizeof(ClockSnapshot) == 0xd0 && offsetof(ClockSnapshot, network_calendar) == 0x58 &&
               offsetof(ClockSnapshot, type) == 0xcd, "time clock snapshot ABI");

static int clock_utc_calendar(time_t seconds, ClockCalendarResult *value) {
    struct tm calendar;
    if (gmtime_s(&calendar, &seconds)) return 0;
    *value = (ClockCalendarResult){0};
    value->calendar.year = calendar.tm_year + 1900;
    value->calendar.month = calendar.tm_mon + 1;
    value->calendar.day = calendar.tm_mday;
    value->calendar.hour = calendar.tm_hour;
    value->calendar.minute = calendar.tm_min;
    value->calendar.second = calendar.tm_sec;
    value->info.weekday = calendar.tm_wday;
    value->info.yearday = calendar.tm_yday;
    memcpy(value->info.zone, "UTC", 3);
    return 1;
}

static uint32_t clock_snapshot(const uint32_t *message, unsigned payload, unsigned end) {
    const uint32_t invalid_time_argument = 0x74;
    // This fixed-size pointer output is one HIPC receive static (C descriptor).
    if (payload + 5 > end || end + 2 > 64 || ((message[1] >> 10) & 15) != 3)
        return invalid_time_argument;
    const uint32_t *descriptor = message + end;
    const uint64_t address = descriptor[0] | ((uint64_t)(descriptor[1] & 0xffff) << 32);
    if ((descriptor[1] >> 16) < sizeof(ClockSnapshot) || !address ||
        address > (1ull << 39) - sizeof(ClockSnapshot))
        return invalid_time_argument;

    for (uint64_t cursor = address; cursor < address + sizeof(ClockSnapshot); ) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)(uintptr_t)cursor, &region, sizeof region) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
            !(region.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return invalid_time_argument;
        cursor = (uintptr_t)region.BaseAddress + region.RegionSize;
    }

    const time_t now = time(NULL);
    ClockCalendarResult utc;
    if (now == (time_t)-1 || !clock_utc_calendar(now, &utc)) return invalid_time_argument;
    ClockSnapshot snapshot = {0};
    snapshot.steady.seconds = rt_ticks() / 19200000;
    snapshot.user_context = (ClockContext){now - snapshot.steady.seconds, snapshot.steady};
    snapshot.network_context = snapshot.user_context;
    snapshot.user_time = snapshot.network_time = now;
    snapshot.user_calendar = snapshot.network_calendar = utc.calendar;
    snapshot.user_info = snapshot.network_info = utc.info;
    memcpy(snapshot.location, "UTC", 3);
    snapshot.type = (uint8_t)message[payload + 4];
    memcpy((void *)(uintptr_t)address, &snapshot, sizeof snapshot);
    return 0;
}

#include "save_fs.inc"
#include "audio_out.inc"

static int applet_command(uint32_t *message, unsigned object, unsigned command,
                          unsigned payload, unsigned descriptor, unsigned data_end) {
    uint32_t result = 0, child = 0, event = 0;
    if (object >= 200 || (object==30 && command==1)) fprintf(stderr,"Applet object %u cmd %u header %#x payloadarg %#x\n",object,command,message[0],message[payload+4]);
    uint64_t value = 0;
    unsigned size = 0;
    if (object == 1 && command == 0) child = 2;
    else if (object == 2 && (command <= 4 || command == 10 || command == 11 ||
                            command == 20 || command == 1000)) child = 10 + command;
    else if (object == 12 && command == 1) { value = 1; size = 8; }
    else if (object == 10 && command == 9) { value = 1; size = 1; }
    else if (object == 10 && command == 5) size = 1;
    else if (object == 10 && command == 6) size = 4;
    else if (object == 10 && command == 0) event = 100;
    else if (object == 10 && command == 1) {
        static const uint32_t messages[] = {15, 30, 31};
        if (applet_messages < 3) {
            value = messages[applet_messages++];
            size = 4;
            if (applet_messages == 3) ResetEvent(applet_event);
        } else result = 0x680;
    } else if (object == 12 && command == 10) { }
    else if (object == 11 && (command == 1 || command == 2 || command == 10 ||
                             command == 11 || command == 12 || command == 13 ||
                             command == 14 || command == 16)) { }
    else if (object == 11 && command == 90) { value=0; size=8; }
    else if (object == 11 && command == 91) event = 101;
    else if (object == 11 && command == 9) event = 105;
    else if (object == 11 && command == 40) { value = 1; size = 8; }
    else if (object == 30 && command == 1) {
        unsigned kind = message[payload+4];
        if (kind != 2) result = 0x480;
        else {
            if(storage_count==32)return 0;
            unsigned slot=storage_count++;
            storage[slot].size=0x88;
            storage[slot].data=calloc(1,0x88);
            if(!storage[slot].data)exit(4);
            uint32_t magic=0xc79497ca; memcpy(storage[slot].data,&magic,4);
            storage[slot].data[4]=1; storage[slot].data[8]=1;
            child=200+slot;
        }
    }
    else if (object == 30 && command == 20) { value=0; size=8; }
    else if (object == 30 && command == 130) event = 102;
    else if (object == 30 && command == 21) { memcpy(&value,"en-US",5); size=8; }
    else if (object == 30 && (command == 22 || command == 67)) { }
    else if (object == 21 && command == 0) {
        library_applet_id=payload+5<=data_end?message[payload+4]:0;
        controller_support_request=catherine_controller_support_default();
        child = 40;
    }
    else if (object == 40 && command == 0) event = 105;
    else if (object == 40 && command == 1) {value=1;size=1;}
    else if (object == 40 && (command == 10 || command == 30 || command == 100)) { }
    else if (object == 40 && command == 101) {
        if (storage_count == 32) return 0;
        unsigned slot = storage_count++;
        storage[slot].size = 12;
        storage[slot].data = calloc(1, 12);
        if (!storage[slot].data) exit(4);
        if(library_applet_id==0xc)
            catherine_controller_support_result(storage[slot].data,controller_support_request,graphics_connected_player_count());
        else storage[slot].data[0] = 1;
        child = 200 + slot;
    }
    else if (object == 21 && command == 10 && payload+6<=data_end) {
        int64_t requested;memcpy(&requested,message+payload+4,8);
        if(requested<0 || requested>1024*1024 || storage_count==32)return 0;
        unsigned slot=storage_count++;
        storage[slot].size=(uint64_t)requested;
        storage[slot].data=calloc(1,requested?(size_t)requested:1);
        if(!storage[slot].data)exit(4);
        child=200+slot;
    } else if(object>=200 && object<200+storage_count && command==0) child=300+object-200;
    else if(object>=300 && object<300+storage_count && command==0) {
        value=storage[object-300].size;size=8;
    } else if(object>=300 && object<300+storage_count && (command==10 || command==11) &&
              ((message[0]>>(command==10 ? 20 : 24))&15)==1 && payload+6<=data_end) {
        int64_t offset;memcpy(&offset,message+payload+4,8);
        unsigned index=descriptor+((message[0]>>16)&15)*2 + (command==11 ? ((message[0]>>20)&15)*3 : 0);
        uint32_t *d=message+index;
        if(command==11) { for(unsigned k=0;k<28;k++) fprintf(stderr," %u:%08x",k,message[k]); fprintf(stderr,"\n"); }
        if(command==11) fprintf(stderr,"descriptor pos %u index %u words %#x %#x %#x %#x %#x %#x\n",descriptor,index,message[2],message[3],message[4],message[5],message[6],message[7]);
        uint64_t address=d[1]|((uint64_t)(d[2]>>28)<<32)|((uint64_t)((d[2]>>2)&0x3fffff)<<36);
        uint64_t length=d[0]|((uint64_t)((d[2]>>24)&15)<<32);
        if (command==11 && !length && ((message[1]>>10)&15)) { uint32_t *recv=message+data_end; address=recv[0]|((uint64_t)(recv[1]&0xffff)<<32); length=recv[1]>>16; }
        if(offset<0 || (uint64_t)offset>storage[object-300].size ||
           length>storage[object-300].size-offset)return 0;
        if (command == 10) {
            memcpy(storage[object-300].data+offset,(void *)(uintptr_t)address,(size_t)length);
            if(library_applet_id==0xc&&offset==0&&length>=6&&storage[object-300].size>=0x430)
                catherine_controller_support_parse(&controller_support_request,storage[object-300].data,(size_t)length);
        }
        else { fprintf(stderr,"Storage read %llu offset %lld address %#llx first %#x\n",length,offset,address,*(uint32_t *)storage[object-300].data); memcpy((void *)(uintptr_t)address,storage[object-300].data+offset,(size_t)length); }
    }
    else {
        fprintf(stderr,"Unhandled applet object %u command %u\n",object,command);
        return 0;
    }
    domain_reply(message,result,value,size,child,event);
    return 1;
}

HANDLE ipc_event(uint32_t handle) {
    if (handle == 100) return applet_event;
    if (handle == 101) return suspended_event;
    if (handle == 102) return gpu_event;
    if (handle == 103) return audio_event;
    if (handle == 104) return output_event;
    if (handle == 105) return launch_event;
    if (handle == 106) return network_state_event;
    if (handle == 107) return network_completion_event;
    return NULL;
}

int ipc_svc(Cpu *cpu, uint32_t immediate) {
    if (!applet_event) {
        applet_event=CreateEventA(NULL,TRUE,TRUE,NULL);
        suspended_event=CreateEventA(NULL,TRUE,FALSE,NULL);
        gpu_event=CreateEventA(NULL,TRUE,FALSE,NULL);
        audio_event=CreateEventA(NULL,TRUE,FALSE,NULL);
        output_event=CreateEventA(NULL,TRUE,FALSE,NULL);
        launch_event=CreateEventA(NULL,TRUE,TRUE,NULL);
        if (!applet_event || !suspended_event || !gpu_event || !audio_event || !output_event || !launch_event) exit(4);
    }
    if (immediate == 0x16 && cpu->x[0] >= SM && cpu->x[0] <= 107) {
        if (cpu->x[0] < 100) is_domain[cpu->x[0]] = 0;
        cpu->x[0] = 0;
        return 1;
    }
    if ((immediate == 0x12 || immediate == 0x17) && ipc_event((uint32_t)cpu->x[0])) { ResetEvent(ipc_event((uint32_t)cpu->x[0])); cpu->x[0]=0; return 1; }
    if (immediate == 0x1f) {
        const char *name = (const char *)(uintptr_t)cpu->x[1];
        if (strcmp(name, "sm:")) {
            fprintf(stderr, "Unknown port %.12s\n", name);
            exit(4);
        }
        cpu->x[0] = 0;
        cpu->x[1] = SM;
        return 1;
    }
    if (immediate != 0x21 || cpu->x[0] < SM || cpu->x[0] >= 100 ||
        (cpu->x[0] > NIFM_REQUEST && !session_type[cpu->x[0]])) return 0;
    uint32_t session = (uint32_t)cpu->x[0];
    uint32_t handle = session_type[session] ? session_type[session] : session;
    uint32_t *m = (uint32_t *)(uintptr_t)cpu->tpidrro;
    unsigned type = m[0] & 0xffff, words = m[1] & 0x3ff, pos = 2;
    if (type == 2 || type == 15) { is_domain[session]=0; cpu->x[0] = 0; return 1; }
    if (m[1] >> 31) {
        uint32_t h = m[pos++];
        pos += (h & 1) * 2 + ((h >> 1) & 15) + ((h >> 5) & 15);
    }
    unsigned descriptor=pos;
    pos += ((m[0] >> 16) & 15) * 2 +
           (((m[0] >> 20) & 15) + ((m[0] >> 24) & 15) + ((m[0] >> 28) & 15)) * 3;
    unsigned payload = type >= 16 ? pos : (pos + 3) & ~3u;
    if (pos + words > 64 || payload > pos + words) goto invalid;
    int control = type == 5 || type == 7;
    unsigned object = 0;
    if (!control && type < 16 && is_domain[session]) {
        if (payload + 4 <= pos + words && (m[payload] & 255) == 2) {
            if (handle == FILESYSTEM) save_close(m[payload + 1]);
            reply(m,0,0,0);
            cpu->x[0]=0;
            return 1;
        }
        if (payload + 4 > pos + words || (m[payload] & 255) != 1) goto invalid;
        object = m[payload + 1];
        payload += 4;
    }
    if (type < 16 && (payload + 4 > pos + words || m[payload] != 0x49434653)) goto invalid;
    uint32_t command = type >= 16 ? type - 16 : m[payload + 2];
    if (control && (command == 2 || command == 4)) {
        LONG clone=InterlockedIncrement(&next_session);
        if(clone>=100)exit(4);
        session_type[clone]=handle;is_domain[clone]=is_domain[session];
        reply(m,0,clone,0);
    }
    else if (control && command == 3) reply(m, 0, 0, 0x8000);
    else if (control && command == 0 && handle >= LOG_MANAGER) {
        is_domain[session] = 1;
        reply(m, 0, 0, 1);
    } else if (handle == SM && command == 0) {
        reply(m, type >= 16, 0, 0);
        puts("Service manager initialized");
    } else if (handle == SM && command == 1) {
        unsigned name = payload + (type >= 16 ? 0 : 4);
        if (name + 2 > pos + words) goto invalid;
        uint32_t service = !memcmp(m + name, "lm\0\0\0\0\0", 8) ? LOG_MANAGER :
                           !memcmp(m + name, "apm\0\0\0\0", 8) ? APM :
                           !memcmp(m + name, "appletOE", 8) ? APPLET :
                           !memcmp(m + name, "aoc:u\0\0", 8) ? AOC :
                           !memcmp(m + name, "pctl\0\0\0", 8) ? PCTL :
                           !memcmp(m + name, "pctl:s\0", 8) ? PCTL_S :
                           !memcmp(m + name, "pctl:a\0", 8) ? PCTL_A :
                           !memcmp(m + name, "pctl:r\0", 8) ? PCTL_R :
                           !memcmp(m + name, "ectx:aw", 8) ? ECTX :
                           !memcmp(m + name, "time:u\0", 8) ? TIME :
                           !memcmp(m + name, "set\0\0\0\0", 8) ? SETTINGS :
                           !memcmp(m + name, "fsp-srv", 8) ? FILESYSTEM :
                           !memcmp(m + name, "nvdrv\0\0", 8) ? NVDRV :
                           !memcmp(m + name, "vi:m\0\0\0", 8) ? VI_MANAGER :
                           !memcmp(m + name, "vi:s\0\0\0", 8) ? VI_SYSTEM :
                           !memcmp(m + name, "vi:u\0\0\0", 8) ? VI_USER :
                           !memcmp(m + name, "audren:u", 8) ? AUDREN :
                           !memcmp(m + name, "audout:u", 8) ? AUDIO_OUT_MANAGER :
                           !memcmp(m + name, "hid\0\0\0\0", 8) ? HID :
                           !memcmp(m + name, "acc:u0\0", 8) ? ACCOUNT :
                           !memcmp(m + name, "friend:u", 8) ? FRIEND :
                           !memcmp(m + name, "nifm:u\0", 8) ? NIFM : 0;
        if (!service) {
            fprintf(stderr, "Requested service %.8s\n", (char *)(m + name));
            exit(4);
        }
        reply(m, type >= 16, service, 0);
    } else if (handle == NIFM && (command == 4 || command == 5) && object <= 1) {
        if (is_domain[session]) domain_reply(m, 0, 0, 0, 2, 0);
        else reply(m, 0, NIFM_GENERAL, 0);
    } else if ((handle == NIFM && object == 3) || handle == NIFM_REQUEST) {
        uint32_t result = 0, value = 0;
        unsigned size = 0;
        switch (command) {
            case 0: value = 1; size = 4; break; // The offline request is no longer pending.
            case 1: result = (112u << 9) | 110u; break;
            case 2: { // GetSystemEventReadableHandles
                memset(m, 0, 256);
                m[1] = (1u << 31) | (is_domain[session] ? 15u : 12u);
                m[2] = 4;
                m[3] = 106;
                m[4] = 107;
                m[is_domain[session] ? 12 : 8] = 0x4f434653;
                cpu->x[0] = 0;
                return 1;
            }
            case 3: case 4:
                SetEvent(network_state_event);
                SetEvent(network_completion_event);
                break;
            case 21: result = (180u << 9) | 110u; break; // No applet for this request result.
            case 5: case 6: case 8: case 10: case 11: case 12: case 13: case 14: case 17: case 23:
                break; // Requirements do not affect the offline result.
            default:
                fprintf(stderr, "Unhandled NIFM request command %u\n", command);
                exit(4);
        }
        if (is_domain[session]) domain_reply(m, result, value, size, 0, 0);
        else { reply(m, 0, 0, value); m[6] = result; }
    } else if ((handle == NIFM && object == 2) || handle == NIFM_GENERAL) {
        uint32_t result = 0, value = 0;
        unsigned size = 1;
        switch (command) {
            case 1: value = 1; size = 4; break; // GetClientId
            case 4: // CreateRequest
                if (!network_state_event) network_state_event = CreateEventA(NULL, FALSE, FALSE, NULL);
                if (!network_completion_event) network_completion_event = CreateEventA(NULL, TRUE, FALSE, NULL);
                if (!network_state_event || !network_completion_event) exit(4);
                ResetEvent(network_state_event);
                ResetEvent(network_completion_event);
                if (is_domain[session]) domain_reply(m, 0, 0, 0, 3, 0);
                else reply(m, 0, NIFM_REQUEST, 0);
                cpu->x[0] = 0;
                return 1;
            case 12: case 18: result = 0xd46e; size = 0; break; // Offline
            case 16: case 17: case 21: break; // Interface availability
            default:
                fprintf(stderr, "Unhandled NIFM general command %u\n", command);
                exit(4);
        }
        if (is_domain[session]) domain_reply(m, result, value, size, 0, 0);
        else { reply(m, 0, 0, value); m[6] = result; }
    } else if (handle == FRIEND) {
        fprintf(stderr,"Friend object %u cmd %u domain %u\n",object,command,is_domain[session]);
        if(is_domain[session] && object==1 && command<=2) domain_reply(m,0,0,0,2+command,0);
        else { fprintf(stderr,"Unhandled friend call\n"); exit(4); }
    } else if (handle == SETTINGS && !is_domain[session] && (command == 1 || command == 5)) {
        unsigned index=descriptor+((m[0]>>16)&15)*2+((m[0]>>20)&15)*3;
        uint32_t *d=m+index;
        uint64_t address=d[1]|((uint64_t)(d[2]>>28)<<32)|((uint64_t)((d[2]>>2)&0x3fffff)<<36);
        uint64_t length=d[0]|((uint64_t)((d[2]>>24)&15)<<32);
        if(!length && ((m[1]>>10)&15)) { d=m+pos+words; address=d[0]|((uint64_t)(d[1]&0xffff)<<32); length=d[1]>>16; }
        if(length>=8) { memset((void *)(uintptr_t)address,0,(size_t)length); memcpy((void *)(uintptr_t)address,"en-US",5); }
        reply(m,0,0,1);
    } else if (handle == SETTINGS && !is_domain[session] && (command == 3 || command == 6)) {
        reply(m,0,0,1);
    } else if (handle == ACCOUNT && is_domain[session]) {
        fprintf(stderr,"Account domain object %u command %u\n",object,command);
        if (command == 100 || command == 140 || command == 150) domain_reply(m,0,0,0,0,0);
        else if (object == 1 && command == 5) domain_reply(m,0,0,0,3,0);
        else if (object == 1 && command == 101) domain_reply(m,0,0,0,2,0);
        else if (object == 2 && command == 0) domain_reply(m,0,0,1,0,0);
        else if (command == 141 || command == 2 || command == 3) {
            unsigned index=descriptor+((m[0]>>16)&15)*2+((m[0]>>20)&15)*3;
            uint32_t *d=m+index;
            uint64_t address=d[1]|((uint64_t)(d[2]>>28)<<32)|((uint64_t)((d[2]>>2)&0x3fffff)<<36);
            uint64_t length=d[0]|((uint64_t)((d[2]>>24)&15)<<32);
            if(!length && ((m[1]>>10)&15)) { d=m+pos+words; address=d[0]|((uint64_t)(d[1]&0xffff)<<32); length=d[1]>>16; }
            if(length>=16) { memset((void *)(uintptr_t)address,0,(size_t)length); *(uint64_t *)(uintptr_t)address=1; }
            domain_reply(m,0,1,4,0,0);
        }
        else if (command == 0 || command == 1) domain_reply(m,0,1,4,0,0);
        else { fprintf(stderr,"Unhandled account domain command %u\n",command); exit(4); }
    } else if (handle == ACCOUNT && !is_domain[session]) {
        fprintf(stderr,"Account command %u\n",command);
        if (command == 100) reply(m,0,0,0);
        else if (command == 0 || command == 1) reply(m,0,0,1);
        else { fprintf(stderr,"Unhandled account command %u\n",command); exit(4); }
    } else if (handle == LOG_MANAGER && is_domain[session] &&
               ((object == 1 && command == 0) || (object == 2 && command <= 1))) {
        memset(m, 0, 256);
        m[1] = 14 + (object == 1);
        m[4] = object == 1;
        m[8] = 0x4f434653;
        if (object == 1) m[12] = 2;
    } else if ((handle == LOG_MANAGER && command == 0) ||
               (handle == LOGGER && command <= 1)) {
        reply(m, type >= 16, handle == LOG_MANAGER ? LOGGER : 0, 0);
    } else if (handle == APM && command == 1) {
        reply(m, 0, 0, 0);
    } else if (handle == APM && command == 0) {
        reply(m, 0, APM_SESSION, 0);
    } else if (handle == APM_SESSION && command == 0) {
        reply(m, 0, 0, 0);
    } else if (handle == APPLET && is_domain[session] && !control) {
        if(object==21 && command==0 && payload+6<=pos+words)
            fprintf(stderr,"Requested library applet ID %#x mode %#x\n",m[payload+4],m[payload+5]);
        if (!applet_command(m,object,command,payload,descriptor,pos+words)) exit(4);
    } else if (handle == AOC && command == 2) {
        if (is_domain[session]) domain_reply(m,0,0,4,0,0);
        else reply(m,0,0,0);
    } else if (handle >= PCTL && handle <= PCTL_R && is_domain[session] && !control) {
        if (object==1 && command<=1) domain_reply(m,0,0,0,2,0);
        else if(object==2 && command==1) domain_reply(m,0,0,0,0,0);
        else {fprintf(stderr,"Unhandled parental-control object %u command %u\n",object,command);exit(4);}
    } else if (handle == TIME && is_domain[session] && object == 1 && command <= 4) {
        domain_reply(m,0,0,0,command+2,0);
    } else if (handle == TIME && is_domain[session] && object == 1 && command == 20) {
        domain_reply(m,0,0,0,0,300);
    } else if (handle == TIME && is_domain[session] && object == 1 && command == 400) {
        uint32_t result = clock_snapshot(m, payload, pos + words);
        domain_reply(m, result, 0, 0, 0, 0);
    } else if (handle == TIME && is_domain[session] && object == 5 && command == 101 &&
               payload + 6 <= pos + words) {
        int64_t timestamp;
        memcpy(&timestamp,m+payload+4,8);
        ClockCalendarResult value;
        if (!clock_utc_calendar((time_t)timestamp, &value)) goto invalid;
        domain_reply(m,0,0,0,0,0);
        m[1]+=sizeof value/4;
        memcpy(m+12,&value,sizeof value);
    } else if (handle == NVDRV && !control && (command==3 || command==8 || command==13)) {
        if(command==8 && !nv_initialized) {
            fputs("NV driver client setup before initialization\n",stderr);
            exit(4);
        }
        if(command==3) nv_initialized=1;
        reply(m,0,0,0);
    } else if ((handle == VI_MANAGER && command == 2) ||
               (handle == VI_SYSTEM && command == 1) || (handle == VI_USER && command == 0)) {
        reply(m,0,VI_APP_DISPLAY,0);
    } else if (handle == VI_APP_DISPLAY && command >= 100 && command <= 103) {
        reply(m,0,command == 100 ? VI_BINDER : command == 101 ? VI_SYSTEM_DISPLAY :
              command == 102 ? VI_MANAGER_DISPLAY : VI_INDIRECT,0);
    } else if (handle == VI_APP_DISPLAY && command == 1010) {
        reply(m,0,0,1);
        m[1]++;
    } else if (handle == AUDREN && !control && (command == 2 || command == 4)) {
        if(is_domain[session]) domain_reply(m,0,0,0,2,0);
        else reply(m,0,AUDIO_DEVICE,0);
    } else if (handle == AUDIO_DEVICE && (command==4 || command==11 || command==12)) {
        reply(m,0,103,0);
        m[2]=2;
    } else if (handle == HID && command == 0 && !is_domain[session]) {
        reply(m,0,HID_RESOURCE,0);
    } else if (handle == HID_RESOURCE && command == 0 && !is_domain[session]) {
        reply(m,0,301,0);
        m[2]=2;
    } else if (handle == HID && (command==201 || command==206) && !is_domain[session]) {
        reply(m,0,0,0);
    } else if (handle == HID && command==205 && !is_domain[session]) {
        reply(m,0,0,1);
    } else if (handle == HID && command==203 && !is_domain[session]) {
        reply(m,0,VIBRATION,0);
    } else if (handle == VIBRATION && command==0 && !is_domain[session]) {
        reply(m,0,0,0);
    } else if (handle == HID && !is_domain[session] &&
               (command==100 || command==102 || command==103 || command==104 || command==109 || command==120)) {
        if(command==100 && payload+5<=pos+words)controller_styles=m[payload+4];
        if(command==103 || command==109)controller_active=1;
        if(command==104)controller_active=0;
        reply(m,0,0,0);
    } else if (handle == HID && !is_domain[session] && command==101) {
        reply(m,0,0,controller_styles);
    } else if (handle == HID && !is_domain[session] && command==121) {
        reply(m,0,0,0);m[1]++;
    } else if (handle == AUDIO_OUT_MANAGER && (command==1 || command==3)) {
        unsigned out_index=descriptor+((m[0]>>16)&15)*2+((m[0]>>20)&15)*3;
        if (((m[0]>>24)&15)==1 && out_index+3<=pos) {
            uint32_t *d=m+out_index;
            uint64_t address=d[1]|((uint64_t)(d[2]>>28)<<32)|((uint64_t)((d[2]>>2)&0x3fffff)<<36);
            uint64_t capacity=d[0]|((uint64_t)((d[2]>>24)&15)<<32);
            if (capacity>=6) memcpy((void *)(uintptr_t)address,"Audio",6);
        }
        uint32_t rate=48000,channels=2;
        if(payload+6<=pos+words){rate=m[payload+4];channels=m[payload+5]&0xffff;if(!rate)rate=48000;if(!channels)channels=2;}
        reply(m,0,AUDIO_OUT,0);
        m[1]+=3;m[8]=rate;m[9]=channels;m[10]=2;m[11]=1;
        audio_open_device(rate, channels);
    } else if (handle == AUDIO_OUT && (command==0 || command==1 || command==2 || command==4 ||
                                        command==9 || command==10 || command==11 || command==12 || command==13)) {
        audio_control(m, command, payload, pos + words);
    } else if (handle == AUDIO_OUT && (command==3 || command==7) && payload+6<=pos+words) {
        audio_append(m, payload, descriptor);
    } else if (handle == AUDIO_OUT && (command==5 || command==8) && ((m[0]>>24)&15)==1) {
        unsigned index = descriptor + ((m[0]>>16)&15)*2 + ((m[0]>>20)&15)*3;
        if (index+3 > pos) goto invalid;
        const uint32_t *d = m + index;
        uint64_t address = d[1] | ((uint64_t)(d[2]>>28)<<32) | ((uint64_t)((d[2]>>2)&0x3fffff)<<36);
        uint64_t capacity = d[0] | ((uint64_t)((d[2]>>24)&15)<<32);
        if (!capacity && ((m[1]>>10)&15)) {
            if (pos + words + 2 > 64) goto invalid;
            d = m + pos + words;
            address = d[0] | ((uint64_t)(d[1]&0xffff)<<32);
            capacity = d[1]>>16;
        }
        audio_get_released(m, address, capacity);
    } else if (handle == FILESYSTEM && is_domain[session] && object == 1 && command == 1) {
        domain_reply(m,0,0,0,0,0);
    } else if (handle == FILESYSTEM && is_domain[session] && object == 1 && command == 1005) {
        domain_reply(m,0,0,4,0,0);
    } else if (handle == FILESYSTEM && is_domain[session] && object == 1 && command == 51) {
        unsigned result = save_mount();
        domain_reply(m, result, 0, 0, result ? 0 : SAVE_FILESYSTEM, 0);
    } else if (handle == FILESYSTEM && is_domain[session] &&
               (object == SAVE_FILESYSTEM || save_file_object(object))) {
        save_command(m, object, command, payload, descriptor, pos + words);
    } else if (handle == FILESYSTEM && is_domain[session] && object == 1 && command == 200) {
        domain_reply(m,0,0,0,2,0);
        puts("Opened game RomFS");
    } else if (handle == FILESYSTEM && is_domain[session] && object == 2) {
        FILE *file=NULL;
        int packed=asset_pak_initialize();
        uint64_t total=0;
        if(packed==1) total=asset_pak_romfs_size();
        else if(packed==0) {
            if(fopen_s(&file,"assets/romfs/romfs.bin","rb") || !file || _fseeki64(file,0,SEEK_END))exit(4);
            int64_t length=_ftelli64(file);if(length<0){fclose(file);exit(4);}total=(uint64_t)length;
        } else {fprintf(stderr,"assets.pak: %s\n",asset_pak_error());exit(4);}
        if(command==4) domain_reply(m,0,total,8,0,0);
        else if(command==0 && ((m[0]>>24)&15)==1 && payload+8<=pos+words) {
            int64_t offset,length;
            memcpy(&offset,m+payload+4,8);memcpy(&length,m+payload+6,8);
            if(length>0x1000) fprintf(stderr,"RomFS read offset=%lld length=%lld\n",offset,length);
            unsigned index=descriptor+((m[0]>>16)&15)*2+((m[0]>>20)&15)*3;
            if(index+3>pos){if(file)fclose(file);fputs("Malformed RomFS descriptor\n",stderr);exit(4);}
            uint32_t *d=m+index;
            uint64_t address=d[1]|((uint64_t)(d[2]>>28)<<32)|((uint64_t)((d[2]>>2)&0x3fffff)<<36);
            uint64_t capacity=d[0]|((uint64_t)((d[2]>>24)&15)<<32);
            int valid=offset>=0 && length>=0 && (uint64_t)offset<=total &&
                (uint64_t)length<=total-(uint64_t)offset && (uint64_t)length<=capacity;
            int read_ok=0;
            if(valid) {
                if(packed) read_ok=asset_pak_read_romfs((void *)(uintptr_t)address,(uint64_t)offset,(size_t)length);
                else read_ok=!_fseeki64(file,offset,SEEK_SET) &&
                    fread((void *)(uintptr_t)address,1,(size_t)length,file)==(size_t)length;
            }
            if(!read_ok) {
                if(file)fclose(file);fprintf(stderr,"RomFS read failed%s%s\n",packed?": ":"",packed?asset_pak_error():"");exit(4);
            }
            domain_reply(m,0,0,0,0,0);
        } else {fprintf(stderr,"Unhandled storage command %u\n",command);if(file)fclose(file);exit(4);}
        if(file)fclose(file);
    } else {
        fprintf(stderr, "Unhandled IPC handle %u object %u command %u type %u\n",
                handle, object, command, type);
        exit(4);
    }
    cpu->x[0] = 0;
    return 1;
invalid:
    fprintf(stderr, "Malformed IPC request, handle %u type %u\n", handle, type);
    exit(4);
}
