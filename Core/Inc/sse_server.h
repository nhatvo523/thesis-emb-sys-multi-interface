/**
 * @file    sse_server.h
 * @brief   Server-Sent Events (SSE) server — LwIP raw TCP, port 8080.
 *
 * Architecture
 * ------------
 * SSE_Server_Init() opens a passive TCP socket on SSE_PORT (8080).
 * When a browser connects and sends an HTTP GET, the server replies with
 * the SSE response header and keeps the connection open.
 *
 * SSE_Push_Snapshot() is called from any FreeRTOS task after writing new
 * data into g_appData.  The caller allocates a heap snapshot, copies
 * g_appData inside an existing critical section, then passes ownership to
 * SSE_Push_Snapshot() which schedules tcp_write() via tcpip_callback().
 *
 * Browser side uses the standard EventSource API:
 *   const es = new EventSource('http://<ip>:8080/');
 *   es.onmessage = e => applyStatus(JSON.parse(e.data));
 *
 * JSON payload format (same as /status.json):
 *   { "s1":{"r":[u16×8],"ok":0|1},
 *     "s2":{"r":[u16×8],"ok":0|1},
 *     "di":[0|1, 0|1, 0|1, 0|1],
 *     "rl":[0|1, 0|1, 0|1, 0|1] }
 */

#ifndef SSE_SERVER_H
#define SSE_SERVER_H

#include "app_data.h"   /* AppData_t — required by SSE_Push_Snapshot() */

#ifdef __cplusplus
extern "C" {
#endif

/** TCP port the SSE endpoint listens on. */
#define SSE_PORT        8080U

/**
 * @brief  Initialise the SSE TCP server.
 *
 * Call once from ethernet_task AFTER MX_LWIP_Init() and WebServer_Init().
 * Opens a passive TCP listener on SSE_PORT.
 * Safe to call from a FreeRTOS task context (schedules init via
 * tcpip_callback so that tcp_new / tcp_bind run inside tcpip_thread).
 */
void SSE_Server_Init(void);

/**
 * @brief  Push a pre-built snapshot to all connected SSE clients.
 *
 * Use this when the caller already holds g_dataMutex and has just written
 * new data — pass the snapshot taken INSIDE the same critical section so
 * only ONE mutex acquisition is needed per update cycle.
 *
 * Usage pattern:
 *   AppData_t *snap = (AppData_t *)pvPortMalloc(sizeof(AppData_t));
 *   if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
 *   {
 *       g_appData.xxx = ...;           // write new data
 *       if (snap) { *snap = g_appData; } // copy inside same lock
 *       osMutexRelease(g_dataMutex);
 *   }
 *   if (snap) { SSE_Push_Snapshot(snap); } // no second mutex needed
 *
 * The snapshot is freed inside tcpip_thread after tcp_write().
 * If snap is NULL, the call is a no-op.
 */
void SSE_Push_Snapshot(AppData_t *snap);

#ifdef __cplusplus
}
#endif

#endif /* SSE_SERVER_H */
