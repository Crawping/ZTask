#ifndef ZTASK_MODULE_H
#define ZTASK_MODULE_H

struct ztask_context;

typedef void * (*ztask_dl_create)(void);
typedef int(*ztask_dl_init)(void * inst, struct ztask_context *, const char * parm, const size_t sz);
typedef void(*ztask_dl_release)(void * inst);
typedef void(*ztask_dl_signal)(void * inst, int signal);

struct ztask_module {
    const char * name;
    void * module;
    ztask_dl_create create;
    ztask_dl_init init;
    ztask_dl_release release;
    ztask_dl_signal signal;
};

void ztask_module_insert(struct ztask_module *mod);
struct ztask_module * ztask_module_query(const char * name);
void * ztask_module_instance_create(struct ztask_module *);
int ztask_module_instance_init(struct ztask_module *, void * inst, struct ztask_context *ctx, const char * parm, const size_t sz);
void ztask_module_instance_release(struct ztask_module *, void *inst);
void ztask_module_instance_signal(struct ztask_module *, void *inst, int signal);

void ztask_module_init(const char *path);

#endif
