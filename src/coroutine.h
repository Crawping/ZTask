#ifndef _COROUTINE_H_
#define _COROUTINE_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
    typedef struct coenviroment * coenv_t;

    coenv_t coroutine_init();
    void coroutine_uninit(coenv_t);

    typedef void *(*coroutine_func)(coenv_t, void *context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz);

    int coroutine_new(coenv_t, coroutine_func, void *context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz);
    int coroutine_current(coenv_t env);

    void *coroutine_resume(coenv_t, int id);
    void *coroutine_yield(coenv_t);

#ifdef __cplusplus
}
#endif

#endif
