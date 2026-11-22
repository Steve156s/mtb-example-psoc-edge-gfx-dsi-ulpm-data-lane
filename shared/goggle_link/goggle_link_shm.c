/*******************************************************************************
* File Name        : goggle_link_shm.c
*
* Description      : See goggle_link_shm.h.
*
*******************************************************************************/
#include <string.h>

#include "goggle_link_shm.h"

#if defined(GL_SHM_HOST_TEST)
/*******************************************************************************
* Host build: static buffer, no barriers / cache maintenance needed
*******************************************************************************/
static gl_shm_t host_shm;
#define GL_SHM_BASE         (&host_shm)
#define GL_DMB()            __sync_synchronize()
#define GL_CLEAN(p, n)      do { (void)(p); (void)(n); } while (0)
#define GL_INVALIDATE(p, n) do { (void)(p); (void)(n); } while (0)

#else
/*******************************************************************************
* Target build
*******************************************************************************/
#include "cybsp.h"

#if !defined(GL_SHM_ADDR)
/* The CM33 and CM55 memory maps (cymem_CM33_0.h / cymem_CM55_0.h generated
 * from the BSP design.modus) give the same physical region different macro
 * names. The CM55 linker script reserves the whole region; the CM33 linker
 * script places .cy_shared_socmem at its start, so the mailbox is put at the
 * end. Override with DEFINES+=GL_SHM_ADDR=0x... if the memory map changes. */
#if defined(__ARM_ARCH_8_1M_MAIN__) && (__ARM_ARCH_8_1M_MAIN__ == 1)  /* CM55 */
#define GL_SHM_ADDR     (CYMEM_CM55_0_m33_m55_shared_START + \
                         CYMEM_CM55_0_m33_m55_shared_SIZE - GL_SHM_RESERVED)
#else                                                                   /* CM33 */
#define GL_SHM_ADDR     (CYMEM_CM33_0_m33_m55_shared_START + \
                         CYMEM_CM33_0_m33_m55_shared_SIZE - GL_SHM_RESERVED)
#endif
#endif /* !defined(GL_SHM_ADDR) */

#define GL_SHM_BASE         ((gl_shm_t*)(uintptr_t)(GL_SHM_ADDR))
#define GL_DMB()            __DMB()

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
#define GL_CLEAN(p, n)      SCB_CleanDCache_by_Addr((volatile void*)(p), (int32_t)(n))
#define GL_INVALIDATE(p, n) SCB_InvalidateDCache_by_Addr((volatile void*)(p), (int32_t)(n))
#else
#define GL_CLEAN(p, n)      do { (void)(p); (void)(n); } while (0)
#define GL_INVALIDATE(p, n) do { (void)(p); (void)(n); } while (0)
#endif

#endif /* GL_SHM_HOST_TEST */

#define GL_READ_RETRIES     (8U)

/* Compile-time size and layout checks */
typedef char gl_shm_fits_reserved[(sizeof(gl_shm_t) <= GL_SHM_RESERVED) ? 1 : -1];
typedef char gl_shm_ring_pow2[((GL_LOG_RING_SIZE & (GL_LOG_RING_SIZE - 1U)) == 0U) ? 1 : -1];

/*******************************************************************************
* Weak port hooks
*******************************************************************************/
__attribute__((weak)) void gl_shm_port_lock(void)
{
}

__attribute__((weak)) void gl_shm_port_unlock(void)
{
}

/*******************************************************************************
* API
*******************************************************************************/
gl_shm_t* gl_shm(void)
{
    return GL_SHM_BASE;
}

void gl_shm_init_owner(void)
{
    gl_shm_t* s = gl_shm();

    memset(s, 0, sizeof(*s));
    s->down.magic     = GL_SHM_MAGIC;
    s->down.brightness = 80U;
    s->up.magic       = GL_SHM_MAGIC;
    s->up.status.battery_pct  = 0xFFU;
    s->up.status.heading_cdeg = 0xFFFFU;
    s->log_head.magic = GL_SHM_MAGIC;
    GL_DMB();
    GL_CLEAN(s, sizeof(*s));
}

bool gl_shm_ready(void)
{
    gl_shm_t* s = gl_shm();

    GL_INVALIDATE(&s->down, GL_SHM_LINE);
    return (GL_SHM_MAGIC == s->down.magic);
}

void gl_shm_down_write(gl_down_fn_t fn, void* ctx)
{
    gl_down_t* d = &gl_shm()->down;

    gl_shm_port_lock();
    d->seq = d->seq + 1U;           /* odd: write in progress */
    GL_DMB();
    GL_CLEAN(d, GL_SHM_LINE);
    fn(d, ctx);
    GL_DMB();
    d->seq = d->seq + 1U;           /* even: stable */
    GL_DMB();
    GL_CLEAN(d, sizeof(*d));
    gl_shm_port_unlock();
}

void gl_shm_up_write(gl_up_fn_t fn, void* ctx)
{
    gl_up_t* u = &gl_shm()->up;

    gl_shm_port_lock();
    u->seq = u->seq + 1U;
    GL_DMB();
    GL_CLEAN(u, GL_SHM_LINE);
    fn(u, ctx);
    GL_DMB();
    u->seq = u->seq + 1U;
    GL_DMB();
    GL_CLEAN(u, sizeof(*u));
    gl_shm_port_unlock();
}

bool gl_shm_down_read(gl_down_t* out)
{
    gl_down_t* d = &gl_shm()->down;

    for (uint32_t i = 0U; i < GL_READ_RETRIES; i++)
    {
        uint32_t s1;
        uint32_t s2;

        GL_INVALIDATE(d, sizeof(*d));
        s1 = d->seq;
        if (0U != (s1 & 1U))
        {
            continue;
        }
        GL_DMB();
        memcpy(out, (const void*)d, sizeof(*out));
        GL_DMB();
        GL_INVALIDATE(d, GL_SHM_LINE);
        s2 = d->seq;
        if ((s1 == s2) && (GL_SHM_MAGIC == out->magic))
        {
            return true;
        }
    }
    return false;
}

bool gl_shm_up_read(gl_up_t* out)
{
    gl_up_t* u = &gl_shm()->up;

    for (uint32_t i = 0U; i < GL_READ_RETRIES; i++)
    {
        uint32_t s1;
        uint32_t s2;

        GL_INVALIDATE(u, sizeof(*u));
        s1 = u->seq;
        if (0U != (s1 & 1U))
        {
            continue;
        }
        GL_DMB();
        memcpy(out, (const void*)u, sizeof(*out));
        GL_DMB();
        GL_INVALIDATE(u, GL_SHM_LINE);
        s2 = u->seq;
        if ((s1 == s2) && (GL_SHM_MAGIC == out->magic))
        {
            return true;
        }
    }
    return false;
}

void gl_shm_log_push(const char* text, size_t len)
{
    gl_shm_t* s = gl_shm();
    uint32_t head;
    uint32_t tail;

    if ((NULL == text) || (0U == len) || (len >= GL_LOG_RING_SIZE))
    {
        return;
    }

    gl_shm_port_lock();
    GL_INVALIDATE(&s->log_tail, GL_SHM_LINE);
    head = s->log_head.head;
    tail = s->log_tail.tail;

    /* free space: one slot is never used so full != empty */
    if ((GL_LOG_RING_SIZE - 1U - ((head - tail) & (GL_LOG_RING_SIZE - 1U))) >= len)
    {
        for (size_t i = 0U; i < len; i++)
        {
            s->log_buf[(head + i) & (GL_LOG_RING_SIZE - 1U)] = text[i];
        }
        GL_CLEAN(s->log_buf, GL_LOG_RING_SIZE);
        GL_DMB();
        s->log_head.head = (head + (uint32_t)len) & (GL_LOG_RING_SIZE - 1U);
        GL_DMB();
        GL_CLEAN(&s->log_head, GL_SHM_LINE);
    }
    gl_shm_port_unlock();
}

size_t gl_shm_log_pop(char* out, size_t cap)
{
    gl_shm_t* s = gl_shm();
    uint32_t head;
    uint32_t tail;
    size_t n = 0U;

    if ((NULL == out) || (0U == cap))
    {
        return 0U;
    }

    GL_INVALIDATE(&s->log_head, GL_SHM_LINE);
    if (GL_SHM_MAGIC != s->log_head.magic)
    {
        return 0U;
    }
    head = s->log_head.head;
    tail = s->log_tail.tail;
    GL_DMB();
    GL_INVALIDATE(s->log_buf, GL_LOG_RING_SIZE);

    while ((tail != head) && (n < cap))
    {
        out[n++] = s->log_buf[tail];
        tail = (tail + 1U) & (GL_LOG_RING_SIZE - 1U);
    }

    GL_DMB();
    s->log_tail.tail = tail;
    GL_DMB();
    GL_CLEAN(&s->log_tail, GL_SHM_LINE);
    return n;
}
