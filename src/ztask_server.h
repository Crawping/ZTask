#ifndef ZTASK_SERVER_H
#define ZTASK_SERVER_H

#include <stdint.h>
#include <stdlib.h>
#include "coroutine.h"
struct ztask_context;
struct ztask_message;
struct ztask_monitor;

void ztask_context_grab(struct ztask_context *);
void ztask_context_reserve(struct ztask_context *ctx);
//获得线程的主协程对象
cort_t ztask_coroutine_main(void);
//获得线程的当前协程对象
cort_t ztask_coroutine_cur(cort_t cur);
//为服务创建一个协程,返回一个session
int ztask_coroutine_new(struct ztask_context * context);
//挂起当前协程
void ztask_coroutine_yield(struct ztask_context * context);
//唤醒一个协程
void ztask_coroutine_resume(struct ztask_context * context,int session);

struct ztask_context * ztask_context_release(struct ztask_context *);
int ztask_context_push(uint32_t handle, struct ztask_message *message);
void ztask_context_send(struct ztask_context * context, void * msg, size_t sz, uint32_t source, int type, int session);
int ztask_context_newsession(struct ztask_context *);
struct message_queue * ztask_context_message_dispatch(struct ztask_monitor *, struct message_queue *, int weight);	// return next queue
int ztask_context_total();
void ztask_context_dispatchall(struct ztask_context * context);	// for ztask_error output before exit

void ztask_context_endless(uint32_t handle);	// for monitor

void ztask_globalinit(void);
void ztask_globalexit(void);
void ztask_initthread(int m);

void ztask_profile_enable(int enable);

#endif
