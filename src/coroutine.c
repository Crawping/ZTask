#include <stddef.h>
#include <assert.h>
#include <math.h>
#include <uv.h>
#include "coroutine.h"


static uv_key_t cur_key = { 0 };

//汇编实现
#define BOTCOT_CALLDECL __cdecl
extern int* BOTCOT_CALLDECL jump_fcontext(fcontext_t* ofc, fcontext_t nfc, int* vp, char preserve_fpu);
extern fcontext_t BOTCOT_CALLDECL make_fcontext(void* sp, size_t size, void(*fn)(int*));

// Detect posix
#if !defined(_WIN32) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__)))
/* UNIX-style OS. ------------------------------------------- */
#   include <unistd.h>
#   define _HAVE_POSIX 1
#endif

#ifdef _WIN32
#   define WIN32_LEAN_AND_LEAN
#   include <Windows.h>
#if defined(__x86_64__) || defined(__x86_64) \
    || defined(__amd64__) || defined(__amd64) \
    || defined(_M_X64) || defined(_M_AMD64)
/* Windows seams not to provide a constant or function
* telling the minimal stacksize */
#   define MIN_STACKSIZE  8 * 1024
#else
#   define MIN_STACKSIZE  4 * 1024
#endif

static size_t getPageSize()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
}

static size_t getMinSize()
{
    return MIN_STACKSIZE;
}

static size_t getMaxSize()
{
    return  1 * 1024 * 1024 * 1024; /* 1GB */
}

static size_t getDefaultSize()
{
    return 64 * 1024;   /* 64Kb */
}

#elif defined(_HAVE_POSIX)
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#if !defined (SIGSTKSZ)
# define SIGSTKSZ (8 * 1024)
# define UDEF_SIGSTKSZ
#endif

static size_t getPageSize()
{
    /* conform to POSIX.1-2001 */
    return (size_t)sysconf(_SC_PAGESIZE);
}

static size_t getMinSize()
{
    return SIGSTKSZ;
}

static size_t getMaxSize()
{
    rlimit limit;
    getrlimit(RLIMIT_STACK, &limit);

    return (size_t)limit.rlim_max;
}

static size_t getDefaultSize()
{
    size_t size;
    size_t maxSize;
    rlimit limit;

    getrlimit(RLIMIT_STACK, &limit);

    size = 8 * getMinSize();
    if (RLIM_INFINITY == limit.rlim_max)
        return size;
    maxSize = getMaxSize();
    return maxSize < size ? maxSize : size;
}
#endif

/* Stack allocation and protection*/
void create_fcontext_stack(void ** sptr, size_t *size)
{
    size_t pages;
    size_t size_;
    void* vp;
    *sptr = NULL;

    if (*size == 0)
        *size = getDefaultSize();
    if (*size <= getMinSize())
        *size = getMinSize();
    assert(*size <= getMaxSize());

    pages = (size_t)floorf((float)(*size) / (float)(getPageSize()));
    assert(pages >= 2);     /* at least two pages must fit into stack (one page is guard-page) */

    size_ = pages * getPageSize();
    assert(size_ != 0 && *size != 0);
    assert(size_ <= *size);

#ifdef _WIN32
    vp = VirtualAlloc(0, size_, MEM_COMMIT, PAGE_READWRITE);
    if (!vp)
        return;

    DWORD old_options;
    VirtualProtect(vp, getPageSize(), PAGE_READWRITE | PAGE_GUARD, &old_options);
#elif defined(_HAVE_POSIX)
# if defined(MAP_ANON)
    vp = mmap(0, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
# else
    vp = mmap(0, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# endif
    if (vp == MAP_FAILED)
        return s;
    mprotect(vp, getPageSize(), PROT_NONE);
#else
    vp = malloc(size_);
    if (!vp)
        return s;
#endif
    //栈由低向上发展
    *sptr = (char*)vp + size_;
    *size = size_;
}

void destroy_fcontext_stack(void * sptr, size_t size)
{
    void* vp;

    assert(size >= getMinSize());
    assert(size <= getMaxSize());

    vp = (char*)sptr - size;

#ifdef _WIN32
    VirtualFree(vp, 0, MEM_RELEASE);
#elif defined(_HAVE_POSIX)
    munmap(vp, size);
#else
    free(vp);
#endif
}

static void mainfunc(coroutine_t* c) {
    //设置协程状态为运行
    c->status = COROUTINE_RUNNING;
    c->cb(c);
    //协程执行完毕,切回原协程
    c->status = COROUTINE_END;
    jump_fcontext(&c->nfc, c->ofc, c, TRUE);
}

coroutine_t *coroutine_new(fcontext_cb cb) {
    coroutine_t *p = (coroutine_t *)malloc(sizeof(*p));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(*p));
    //分配栈空间
    create_fcontext_stack(&p->sptr, &p->ssize);
    //生成上下文
    p->nfc = make_fcontext(p->sptr, p->ssize, mainfunc);
    //保存用户回调
    p->cb = cb;
    //设置初始状态
    p->status = COROUTINE_READY;
    return p;
}
int coroutine_resume(coroutine_t *c) {
    if (!c)
        return 0;
    if (c->status != COROUTINE_SUSPEND && c->status != COROUTINE_READY)
        return 0;
    //保存当前协程到线程
    uv_key_set(&cur_key, c);
    jump_fcontext(&c->ofc, c->nfc, c, TRUE);
    if (c->status == COROUTINE_END) {
        destroy_fcontext_stack(c->sptr, c->ssize);
        free(c);
        return 1;
    }
    return 0;
}

void coroutine_yield() {
    coroutine_t *cur = uv_key_get(&cur_key);
    //设置协程状态为挂起
    cur->status = COROUTINE_SUSPEND;
    jump_fcontext(&cur->nfc, cur->ofc, cur, TRUE);
}
void coroutine_init() {
    uv_key_create(&cur_key);
}
coroutine_t *coroutine_running() {
    return uv_key_get(&cur_key);
}
