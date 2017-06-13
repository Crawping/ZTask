﻿#include "ztask.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MEMORY_WARNING_REPORT (1024 * 1024 * 32)

struct snlua {
    lua_State * L;
    struct ztask_context * ctx;
    size_t mem;
    size_t mem_report;
    size_t mem_limit;
};

// LUA_CACHELIB may defined in patched lua for shared proto
#ifdef LUA_CACHELIB

#define codecache luaopen_cache

#else

static int
cleardummy(lua_State *L) {
    return 0;
}

static int
codecache(lua_State *L) {
    luaL_Reg l[] = {
        { "clear", cleardummy },
        { "mode", cleardummy },
        { NULL, NULL },
    };
    luaL_newlib(L, l);
    lua_getglobal(L, "loadfile");
    lua_setfield(L, -2, "loadfile");
    return 1;
}

#endif

static int
traceback(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg)
        luaL_traceback(L, L, msg, 1);
    else {
        lua_pushliteral(L, "(no error message)");
    }
    return 1;
}

static void
report_launcher_error(struct ztask_context *ctx) {
    // sizeof "ERROR" == 5
    ztask_sendname(ctx, 0, ".launcher", PTYPE_TEXT, 0, "ERROR", 5);
}

static const char *
optstring(struct ztask_context *ctx, const char *key, const char * str) {
    const char * ret = ztask_command(ctx, "GETENV", key);
    if (ret == NULL) {
        return str;
    }
    return ret;
}

LUAMOD_API int luaopen_ztask_core(lua_State *L);
LUAMOD_API int luaopen_profile(lua_State *L);
LUAMOD_API int luaopen_memory(lua_State *L);
LUAMOD_API int luaopen_socketdriver(lua_State *L);
LUAMOD_API int luaopen_lpeg(lua_State *L);
LUAMOD_API int luaopen_sproto_core(lua_State *L);
LUAMOD_API int luaopen_netpack(lua_State *L);
LUAMOD_API int luaopen_mysqlaux_c(lua_State *L);
LUAMOD_API int luaopen_mongo_driver(lua_State *L);
LUAMOD_API int luaopen_crypt(lua_State *L);
LUAMOD_API int luaopen_sharedata_core(lua_State *L);
LUAMOD_API int luaopen_multicast_core(lua_State *L);
LUAMOD_API int luaopen_debugchannel(lua_State *L);
LUAMOD_API int luaopen_cluster_core(lua_State *L);
LUAMOD_API int luaopen_clientsocket(lua_State *L);
LUAMOD_API int luaopen_bson(lua_State *L);

static int
init_cb(struct snlua *l, struct ztask_context *ctx, const char * args, size_t sz) {
    lua_State *L = l->L;
    l->ctx = ctx;
    lua_gc(L, LUA_GCSTOP, 0);
    lua_pushboolean(L, 1);  /* signal for libraries to ignore env. vars. */
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
    luaL_openlibs(L);
    lua_pushlightuserdata(L, ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, "ztask_context");
    luaL_requiref(L, "ztask.codecache", codecache, 0);
    lua_pop(L, 1);

    //加载内置库
    luaopen_ztask_core(L);
    luaopen_profile(L);
    luaopen_memory(L);
    luaopen_socketdriver(L);
    luaopen_lpeg(L);
    luaopen_sproto_core(L);
    luaopen_netpack(L);
    luaopen_mysqlaux_c(L);
    luaopen_mongo_driver(L);
    luaopen_crypt(L);
    luaopen_sharedata_core(L);
    luaopen_multicast_core(L);
    luaopen_debugchannel(L);
    luaopen_cluster_core(L);
    luaopen_clientsocket(L);
    luaopen_bson(L);
    //内置库加载完毕

    const char *path = optstring(ctx, "lua_path", "./lualib/?.lua;./lualib/?/init.lua");
    lua_pushstring(L, path);
    lua_setglobal(L, "LUA_PATH");
    const char *cpath = optstring(ctx, "lua_cpath", "./luaclib/?.so");
    lua_pushstring(L, cpath);
    lua_setglobal(L, "LUA_CPATH");
    const char *service = optstring(ctx, "luaservice", "./service/?.lua");
    lua_pushstring(L, service);
    lua_setglobal(L, "LUA_SERVICE");
    const char *preload = ztask_command(ctx, "GETENV", "preload");
    lua_pushstring(L, preload);
    lua_setglobal(L, "LUA_PRELOAD");

    lua_pushcfunction(L, traceback);
    assert(lua_gettop(L) == 1);

    const char * loader = optstring(ctx, "lualoader", "./lualib/loader.lua");

    int r = luaL_loadfile(L, loader);
    if (r != LUA_OK) {
        ztask_error(ctx, "Can't load %s : %s", loader, lua_tostring(L, -1));
        report_launcher_error(ctx);
        return 1;
    }
    lua_pushlstring(L, args, sz);
    r = lua_pcall(L, 1, 0, 1);
    if (r != LUA_OK) {
        ztask_error(ctx, "lua loader error : %s", lua_tostring(L, -1));
        report_launcher_error(ctx);
        return 1;
    }
    lua_settop(L, 0);
    if (lua_getfield(L, LUA_REGISTRYINDEX, "memlimit") == LUA_TNUMBER) {
        size_t limit = lua_tointeger(L, -1);
        l->mem_limit = limit;
        ztask_error(ctx, "Set memory limit to %.2f M", (float)limit / (1024 * 1024));
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "memlimit");
    }
    lua_pop(L, 1);

    lua_gc(L, LUA_GCRESTART, 0);

    return 0;
}

static int
launch_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    assert(type == 0 && session == 0);
    struct snlua *l = ud;
    ztask_callback(context, NULL, NULL);
    int err = init_cb(l, context, msg, sz);
    if (err) {
        ztask_command(context, "EXIT", NULL);
    }

    return 0;
}

int
snlua_init(struct snlua *l, struct ztask_context *ctx, const char * args, const size_t sz) {
    char * tmp = ztask_malloc(sz);
    memcpy(tmp, args, sz);
    ztask_callback(ctx, l, launch_cb);
    const char * self = ztask_command(ctx, "REG", NULL);
    uint32_t handle_id = strtoul(self + 1, NULL, 16);
    // it must be first message
    ztask_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY, 0, tmp, sz);
    return 0;
}

static void *
lalloc(void * ud, void *ptr, size_t osize, size_t nsize) {
    struct snlua *l = ud;
    size_t mem = l->mem;
    l->mem += nsize;
    if (ptr)
        l->mem -= osize;
    if (l->mem_limit != 0 && l->mem > l->mem_limit) {
        if (ptr == NULL || nsize > osize) {
            l->mem = mem;
            return NULL;
        }
    }
    if (l->mem > l->mem_report) {
        l->mem_report *= 2;
        ztask_error(l->ctx, "Memory warning %.2f M", (float)l->mem / (1024 * 1024));
    }
    return ztask_lalloc(ptr, osize, nsize);
}

struct snlua *
    snlua_create(void) {
    struct snlua * l = ztask_malloc(sizeof(*l));
    memset(l, 0, sizeof(*l));
    l->mem_report = MEMORY_WARNING_REPORT;
    l->mem_limit = 0;
    l->L = lua_newstate(lalloc, l);
    return l;
}

void
snlua_release(struct snlua *l) {
    lua_close(l->L);
    ztask_free(l);
}

void
snlua_signal(struct snlua *l, int signal) {
    ztask_error(l->ctx, "recv a signal %d", signal);
    if (signal == 0) {
#ifdef lua_checksig
        // If our lua support signal (modified lua version by ztask), trigger it.
        ztask_sig_L = l->L;
#endif
    }
    else if (signal == 1) {
        ztask_error(l->ctx, "Current Memory %.3fK", (float)l->mem / 1024);
    }
}
