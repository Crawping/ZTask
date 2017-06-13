#ifndef ZTASK_ENV_H
#define ZTASK_ENV_H

const char * ztask_getenv(const char *key);
void ztask_setenv(const char *key, const char *value);

void ztask_env_init();

#endif
