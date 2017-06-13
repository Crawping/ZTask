#include"ztask.h"

#include<assert.h>
#include<string.h>
#include<stdlib.h>
#include<stdio.h>

int snc_init(struct snc *l, struct ztask_context *ctx, const char * args, const size_t sz);
struct snc *snc_create(void);
void snc_release(struct snc *l);
void snc_signal(struct snc *l, int signal);



