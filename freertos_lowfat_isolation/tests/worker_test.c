#include <assert.h>
#include <stdio.h>
#include "../Core/Src/bench_fast_worker.c"
static void work(void *p){(void)p;}
int main(void){
 int stack[8],tcbs[BENCH_SUBSCRIBERS+1];
 TaskParameters_t p={work,stack,&tcbs[0],4};
 assert(!bench_fast_notify(0));assert(!bench_fast_notify(BENCH_SUBSCRIBERS+1));
 assert(!bench_fast_create(BENCH_SUBSCRIBERS+1,&p));assert(!bench_fast_create(0,NULL));
 p.uxPriority|=portPRIVILEGE_BIT;assert(!bench_fast_create(0,&p));p.uxPriority=4;
 p.pvTaskCode=NULL;assert(!bench_fast_create(0,&p));p.pvTaskCode=work;
 p.puxStackBuffer=NULL;assert(!bench_fast_create(0,&p));p.puxStackBuffer=stack;
 p.pxTaskBuffer=NULL;assert(!bench_fast_create(0,&p));
 for(unsigned i=0;i<=BENCH_SUBSCRIBERS;i++){
  p.pxTaskBuffer=&tcbs[i];assert(bench_fast_create(i,&p));assert(!bench_fast_create(i,&p));
  assert(bench_fast_notify(i));assert(last_handle==&tcbs[i]);
 }
 assert(created==BENCH_SUBSCRIBERS+1 && notified==created);
 puts("WORKER HOST PASS: slot bounds, missing/duplicate creation, unprivileged flag, handle routing and notification arguments");
}
