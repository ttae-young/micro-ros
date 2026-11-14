#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PRIVILEGED_DATA
#define portPRIVILEGE_BIT 0x80000000U
#define pdPASS 1
#define eIncrement 2
typedef void *TaskHandle_t;
typedef struct { void (*pvTaskCode)(void*); void *puxStackBuffer,*pxTaskBuffer; uint32_t uxPriority; } TaskParameters_t;
static unsigned created,notified;
static TaskHandle_t last_handle;
static int xTaskCreateRestrictedStatic(const TaskParameters_t *p,TaskHandle_t *h){ *h=p->pxTaskBuffer;created++;return pdPASS; }
static int xTaskGenericNotify(TaskHandle_t h,unsigned index,unsigned value,unsigned action,void *previous){assert(h && index==0 && value==0 && action==eIncrement && !previous);last_handle=h;notified++;return pdPASS;}
