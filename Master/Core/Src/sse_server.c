/**
 * @file    sse_server.c
 * @brief   Server-Sent Events (SSE) server — LwIP raw TCP, port 8080.
 *
 * Design notes
 * ------------
 * All LwIP raw TCP callbacks (sse_accept_cb, sse_recv_cb, sse_err_cb,
 * sse_do_push) execute inside tcpip_thread — the only context from which
 * LwIP raw API is safe to call.
 *
 * SSE_Push_Snapshot() is the only entry point called from outside
 * tcpip_thread.  It does NOT access s_clients[] directly; instead it calls
 * tcpip_callback(sse_do_push, snap) which posts a message to tcpip_thread's
 * mailbox.  sse_do_push() then accesses s_clients[] safely.
 *
 * SSE_Server_Init() schedules the TCP setup (tcp_new / tcp_bind /
 * tcp_listen / tcp_accept) via tcpip_callback() so it also runs inside
 * tcpip_thread, matching the thread model used by httpd_init().
 *
 * Memory
 * ------
 * Each SSE_Push_Snapshot() call receives an already-allocated AppData_t
 * heap block (pvPortMalloc from the caller).  The block is always freed
 * inside sse_do_push() regardless of success or error.
 */

#include "sse_server.h"
#include "app_data.h"           /* AppData_t                                 */
#include "lwip/tcp.h"           /* tcp_new, tcp_bind, tcp_listen, tcp_write  */
#include "lwip/tcpip.h"         /* tcpip_callback                            */
#include "FreeRTOS.h"           /* pvPortMalloc / vPortFree                  */
#include <stdio.h>              /* snprintf                                   */
#include <string.h>             /* memset                                     */

/* ===========================================================================
 * Private constants
 * ========================================================================= */

/** Maximum number of concurrent SSE client connections. */
#define SSE_MAX_CLIENTS     3U

/* SSE HTTP response header — sent once per connection, then the socket stays
 * open and we push "data: <json>\n\n" frames whenever data changes.         */
static const char s_sse_hdr[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n";

/* ===========================================================================
 * Private state  (accessed only from tcpip_thread via callbacks)
 * ========================================================================= */

/** Active client PCBs.  NULL = free slot. */
static struct tcp_pcb *s_clients[SSE_MAX_CLIENTS];

/* ===========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief  Build a complete SSE "data:" frame from an AppData_t snapshot.
 *
 * Output format:
 *   data: {"s1":{"r":[…],"ok":N},"s2":{"r":[…],"ok":N},"di":[…],"rl":[…]}\n\n
 *
 * JSON structure matches applyStatus() in index.shtml so no JS changes
 * are needed beyond swapping setInterval for EventSource.
 *
 * @param[in]  d    Pointer to AppData_t snapshot.
 * @param[out] buf  Output buffer.
 * @param[in]  max  Size of @p buf in bytes.
 * @return  Number of bytes written (excluding NUL), or 0 on error.
 */
static int build_sse_frame(const AppData_t *d, char *buf, int max)
{
    /* ── JSON body ──────────────────────────────────────────────────── */
    char json[300];
    int jlen = snprintf(json, (int)sizeof(json),
        "{\"s1\":{\"r\":[%u,%u,%u,%u,%u,%u,%u,%u],\"ok\":%u},"
         "\"s2\":{\"r\":[%u,%u,%u,%u,%u,%u,%u,%u],\"ok\":%u},"
         "\"di\":[%u,%u,%u,%u],"
         "\"rl\":[%u,%u,%u,%u]}",
        /* Slave 1 registers */
        (unsigned)d->mb_slave[0].holding_reg[0],
        (unsigned)d->mb_slave[0].holding_reg[1],
        (unsigned)d->mb_slave[0].holding_reg[2],
        (unsigned)d->mb_slave[0].holding_reg[3],
        (unsigned)d->mb_slave[0].holding_reg[4],
        (unsigned)d->mb_slave[0].holding_reg[5],
        (unsigned)d->mb_slave[0].holding_reg[6],
        (unsigned)d->mb_slave[0].holding_reg[7],
        (unsigned)d->mb_slave[0].connected,
        /* Slave 2 registers */
        (unsigned)d->mb_slave[1].holding_reg[0],
        (unsigned)d->mb_slave[1].holding_reg[1],
        (unsigned)d->mb_slave[1].holding_reg[2],
        (unsigned)d->mb_slave[1].holding_reg[3],
        (unsigned)d->mb_slave[1].holding_reg[4],
        (unsigned)d->mb_slave[1].holding_reg[5],
        (unsigned)d->mb_slave[1].holding_reg[6],
        (unsigned)d->mb_slave[1].holding_reg[7],
        (unsigned)d->mb_slave[1].connected,
        /* Digital inputs   bit 0..3 */
        (unsigned)((d->di_state    >> 0U) & 0x01U),
        (unsigned)((d->di_state    >> 1U) & 0x01U),
        (unsigned)((d->di_state    >> 2U) & 0x01U),
        (unsigned)((d->di_state    >> 3U) & 0x01U),
        /* Relay outputs    bit 0..3 */
        (unsigned)((d->relay_state >> 0U) & 0x01U),
        (unsigned)((d->relay_state >> 1U) & 0x01U),
        (unsigned)((d->relay_state >> 2U) & 0x01U),
        (unsigned)((d->relay_state >> 3U) & 0x01U)
    );

    if (jlen <= 0 || jlen >= (int)sizeof(json))
    {
        return 0;
    }

    /* ── Wrap in SSE "data:" line ───────────────────────────────────── */
    int len = snprintf(buf, max, "data: %.*s\n\n", jlen, json);
    return (len > 0 && len < max) ? len : 0;
}

/**
 * @brief  Remove a PCB from the client slot array.
 *
 * Does NOT call any LwIP API — callers handle closing/aborting themselves.
 *
 * @param  pcb  PCB to remove.
 */
static void client_slot_clear(const struct tcp_pcb *pcb)
{
    for (uint8_t i = 0U; i < SSE_MAX_CLIENTS; i++)
    {
        if (s_clients[i] == pcb)
        {
            s_clients[i] = NULL;
            return;
        }
    }
}

/* ===========================================================================
 * LwIP raw TCP callbacks  (all run in tcpip_thread context)
 * ========================================================================= */

/**
 * @brief  Error callback.
 *
 * Called by LwIP when a connection-level error occurs (e.g. RST received,
 * memory error).  The PCB has already been freed at this point — do NOT
 * call any tcp_* functions on it.  Just remove it from our slot array.
 */
static void sse_err_cb(void *arg, err_t err)
{
    (void)err;
    /* arg was set to the PCB pointer in sse_accept_cb; use it only as a
     * key for lookup — never dereference it (PCB is freed by LwIP).       */
    client_slot_clear((struct tcp_pcb *)arg);
}

/**
 * @brief  Receive callback.
 *
 * Browsers opening an EventSource connection send an HTTP GET request.
 * We discard all received data — the SSE spec says the client only sends
 * the initial HTTP header, then listens.
 *
 * p == NULL signals that the remote side has closed the connection (FIN).
 */
static err_t sse_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p,
                          err_t err)
{
    (void)arg;
    (void)err;

    if (p == NULL)
    {
        /* Client sent FIN — close our side gracefully. */
        client_slot_clear(pcb);
        tcp_arg (pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err (pcb, NULL);
        tcp_close(pcb);
    }
    else
    {
        /* Acknowledge and discard received bytes. */
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
    }
    return ERR_OK;
}

/**
 * @brief  Accept callback.
 *
 * Stores the new client PCB, registers per-connection callbacks, and
 * sends the SSE HTTP response header.
 */
static err_t sse_accept_cb(void *arg, struct tcp_pcb *new_pcb, err_t err)
{
    (void)arg;

    if (err != ERR_OK || new_pcb == NULL)
    {
        return ERR_VAL;
    }

    /* Find a free client slot. */
    int8_t slot = -1;
    for (uint8_t i = 0U; i < SSE_MAX_CLIENTS; i++)
    {
        if (s_clients[i] == NULL)
        {
            slot = (int8_t)i;
            break;
        }
    }

    if (slot < 0)
    {
        /* No capacity — reject connection immediately. */
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    s_clients[(uint8_t)slot] = new_pcb;

    /* Pass PCB as opaque arg so sse_err_cb can identify it. */
    tcp_arg (new_pcb, (void *)new_pcb);
    tcp_recv(new_pcb, sse_recv_cb);
    tcp_err (new_pcb, sse_err_cb);

    /* Send SSE header — static string, no copy needed (TCP_WRITE_FLAG_COPY=0).
     * LwIP will reference s_sse_hdr until the bytes are ACKed; the const
     * static buffer lives for the entire firmware lifetime so this is safe. */
    err_t we = tcp_write(new_pcb,
                         s_sse_hdr,
                         (u16_t)(sizeof(s_sse_hdr) - 1U),
                         0U);
    if (we == ERR_OK)
    {
        tcp_output(new_pcb);
    }

    return ERR_OK;
}

/* ===========================================================================
 * tcpip_callback targets  (scheduled from FreeRTOS tasks)
 * ========================================================================= */

/**
 * @brief  Push one SSE frame to all connected clients.
 *
 * Runs inside tcpip_thread (scheduled by tcpip_callback in
 * SSE_Push_Snapshot).  Frees the snapshot regardless of outcome.
 */
static void sse_do_push(void *arg)
{
    AppData_t *snap = (AppData_t *)arg;

    char frame[340];
    int  flen = build_sse_frame(snap, frame, (int)sizeof(frame));

    vPortFree(snap);   /* always free snapshot before returning */

    if (flen <= 0)
    {
        return;
    }

    for (uint8_t i = 0U; i < SSE_MAX_CLIENTS; i++)
    {
        struct tcp_pcb *pcb = s_clients[i];
        if (pcb == NULL)
        {
            continue;
        }

        err_t e = tcp_write(pcb,
                            frame,
                            (u16_t)flen,
                            TCP_WRITE_FLAG_COPY); /* copy: frame is on task stack */
        if (e == ERR_OK)
        {
            tcp_output(pcb);
        }
        else if (e == ERR_MEM)
        {
            /* TCP send buffer is full (slow client) — skip this frame.
             * Do NOT abort: buffer-full is transient, not a dead connection.
             * The next push will try again; the client catches up via SSE
             * retry semantics automatically. */
        }
        else
        {
            /* Genuine error (RST, connection gone) — abort and clear slot. */
            s_clients[i] = NULL;
            tcp_arg (pcb, NULL);
            tcp_recv(pcb, NULL);
            tcp_err (pcb, NULL);
            tcp_abort(pcb);
        }
    }
}

/**
 * @brief  TCP server setup — runs inside tcpip_thread.
 *
 * Scheduled by SSE_Server_Init() via tcpip_callback().
 */
static void sse_server_setup(void *arg)
{
    (void)arg;

    memset(s_clients, 0, sizeof(s_clients));

    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL)
    {
        return;
    }

    if (tcp_bind(pcb, IP_ADDR_ANY, (u16_t)SSE_PORT) != ERR_OK)
    {
        tcp_abort(pcb);
        return;
    }

    struct tcp_pcb *lpcb = tcp_listen_with_backlog(pcb,
                                                    (u8_t)SSE_MAX_CLIENTS);
    if (lpcb == NULL)
    {
        /* tcp_listen_with_backlog frees pcb on failure. */
        return;
    }

    tcp_accept(lpcb, sse_accept_cb);
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

void SSE_Server_Init(void)
{
    /* Schedule setup in tcpip_thread — the only safe context for raw TCP. */
    tcpip_callback(sse_server_setup, NULL);
}

void SSE_Push_Snapshot(AppData_t *snap)
{
    if (snap == NULL)
    {
        return;   /* caller could not allocate — silently skip */
    }
    /* Schedule push inside tcpip_thread — safe to call tcp_write(). */
    tcpip_callback(sse_do_push, snap);
}
