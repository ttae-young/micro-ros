#ifndef WORKER_DOMAIN_H
#define WORKER_DOMAIN_H
#define APP_WAIT 0
#define APP_PUBLISH 1
#define APP_RECEIVE 2
#define APP_DONE_WAIT 3
#define DOMAIN_NORMAL 0
#define DOMAIN_READ 1
#define DOMAIN_WRITE 2
#define DOMAIN_DONE 0x100
#define DOMAIN_DENIED 0x101
#define DOMAIN_BLOCKED 0x80000001
#define DOMAIN_WRITE_OK 0x5a5a
#ifndef __ASSEMBLER__
#include <stdint.h>
extern volatile const uint32_t bench_policy_mode;
void publisher_app_task(void *owner);
void subscriber_app_task(void *owner);
extern const unsigned char pub_load_site[],pub_store_site[],pub_commit_site[];
extern const unsigned char sub_load_site[],sub_store_site[],sub_commit_site[];
#endif
#endif
