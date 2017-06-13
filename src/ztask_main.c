#include "ztask.h"
#include "ztask_env.h"
#include "ztask_server.h"
#include "lualib/luashrtbl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>
#include <assert.h>

ZTASK_EXTERN void ztask_init() {
    luaS_initshr();
    ztask_globalinit();
    ztask_env_init();
}
ZTASK_EXTERN void ztask_uninit() {
    ztask_globalexit();
    luaS_exitshr();
}

