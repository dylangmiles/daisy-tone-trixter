/* audio/usbd_override/usbd_conf.h — SHADOW of libDaisy's src/usbd/usbd_conf.h.
 *
 * ⚠ THE ONLY CHANGE vs libDaisy's copy: USBD_MAX_NUM_INTERFACES 1 -> 8.
 *
 * Why: ST's USBD_StdItfReq (usbd_ctlreq.c:181) gates EVERY interface request with
 *   `LOBYTE(wIndex) <= USBD_MAX_NUM_INTERFACES`. At 1, the host's SET_INTERFACE to any interface
 * numbered >1 is STALLED by the core before it reaches the class — which kills the composite audio
 * stream (its AudioStreaming interface is IF3). libDaisy defines the macro UNGUARDED, so a -D cannot
 * override it; but both files share the include guard `__USBD_CONF__H__`, so this copy (placed earlier
 * on the include path via -Iaudio/usbd_override) wins and libDaisy's is skipped. The macro's ONLY use
 * in the whole core is that one comparison (not array sizing), and only usbd_ctlreq.c reads it — which
 * the Makefile recompiles in this build so it picks up the 8. libDaisy stays pristine (not forked).
 *
 * Keep this byte-identical to libDaisy's usbd_conf.h except the one line, and re-sync if libDaisy's pin
 * ever moves. Source: libDaisy @ e1f740a, src/usbd/usbd_conf.h.
 */
#ifndef __USBD_CONF__H__
#define __USBD_CONF__H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

#define USBD_MAX_NUM_INTERFACES 8U      /* ⚠ libDaisy ships 1U — see the header comment above */
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 512U
#define USBD_SUPPORT_USER_STRING_DESC 1U
#define USBD_DEBUG_LEVEL 3U
#define USBD_LPM_ENABLED 0U
#define USBD_SELF_POWERED 1U

/* FS and HS identification */
#define DEVICE_FS 0
#define DEVICE_HS 1

/* Memory management macros */
#define USBD_malloc malloc
#define USBD_free free
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_Delay HAL_Delay

#if(USBD_DEBUG_LEVEL > 0)
#define USBD_UsrLog(...) \
    printf(__VA_ARGS__); \
    printf("\n");
#else
#define USBD_UsrLog(...)
#endif

#if(USBD_DEBUG_LEVEL > 1)
#define USBD_ErrLog(...) \
    printf("ERROR: ");   \
    printf(__VA_ARGS__); \
    printf("\n");
#else
#define USBD_ErrLog(...)
#endif

#if(USBD_DEBUG_LEVEL > 2)
#define USBD_DbgLog(...) \
    printf("DEBUG : ");  \
    printf(__VA_ARGS__); \
    printf("\n");
#else
#define USBD_DbgLog(...)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CONF__H__ */
