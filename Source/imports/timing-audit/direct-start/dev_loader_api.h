#ifndef CATHERINE_DEV_LOADER_API_H
#define CATHERINE_DEV_LOADER_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CATHERINE_DEV_LOADER_VERSION 1u
enum CatherineDevAreaKind { CATHERINE_DEV_PUZZLE=1,CATHERINE_DEV_BAR=2,CATHERINE_DEV_LANDING=3,CATHERINE_DEV_ROOM=4,CATHERINE_DEV_CHECKPOINT=5,CATHERINE_DEV_BABEL=6,CATHERINE_DEV_COLOSSEUM=7,CATHERINE_DEV_RAPUNZEL=8 };
enum CatherineDevLoadCapabilities { CATHERINE_DEV_CAN_LOAD=1u,CATHERINE_DEV_CAN_KARMA=2u,CATHERINE_DEV_CAN_DIFFICULTY=4u,CATHERINE_DEV_CAN_ROUTE=8u,CATHERINE_DEV_CAN_STAGE_RECIPE=16u };
enum CatherineDevLoadPhase { CATHERINE_DEV_IDLE=0,CATHERINE_DEV_QUEUED=1,CATHERINE_DEV_WAIT_SAFE=2,CATHERINE_DEV_READING=3,CATHERINE_DEV_APPLYING=4,CATHERINE_DEV_TRANSITION=5,CATHERINE_DEV_COMPLETE=6,CATHERINE_DEV_ERROR=7 };
typedef struct CatherineDevTarget {
 uint32_t version,id,kind,capabilities;
 int32_t slot_index,day,stage,substage,difficulty,route;
 char label[128],state_description[256];
} CatherineDevTarget;
typedef struct CatherineDevLoadRequest {
 uint32_t version,target_id;
 int32_t karma_enabled,karma_value,difficulty,route;
 uint32_t reserved[4];
} CatherineDevLoadRequest;
typedef struct CatherineDevLoadStatus {
 uint32_t version,phase,busy,request_id,target_id;
 int32_t error_code;
 char message[256];
} CatherineDevLoadStatus;
uint32_t catherine_dev_loader_count(void);
int catherine_dev_loader_describe(uint32_t index,CatherineDevTarget *out);
int catherine_dev_loader_request(const CatherineDevLoadRequest *request);
void catherine_dev_loader_status(CatherineDevLoadStatus *out);
/* Runtime-native service integration is supplied on the guest main thread only. */
#ifdef __cplusplus
}
#endif
#endif
