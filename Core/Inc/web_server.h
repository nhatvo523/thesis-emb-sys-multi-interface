/**
 * @file    web_server.h
 * @brief   LwIP Raw HTTPD web server public interface.
 *
 *          The server exposes:
 *            - GET /          → index.shtml  (SSI-processed dashboard)
 *            - GET /index.shtml
 *            - GET /status.json → live JSON snapshot of g_appData (custom FS)
 *            - GET /relay.cgi?r=[0-3]&v=[0-1]  → relay control + JSON reply
 *
 *          Requires in lwipopts.h:
 *            #define LWIP_HTTPD            1
 *            #define LWIP_HTTPD_CGI        1
 *            #define LWIP_HTTPD_SSI        1
 *            #define LWIP_HTTPD_SSI_INCLUDE_TAG 0
 *            #define LWIP_HTTPD_DYNAMIC_HEADERS 1
 *            #define LWIP_HTTPD_CUSTOM_FILES    1   ← must be added by user
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "app_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise and start the HTTP web server.
 *
 *         MUST be called from the Ethernet task after MX_LWIP_Init() returns.
 *         Internally:
 *           1. Calls httpd_init()            → opens TCP port 80.
 *           2. Calls http_set_ssi_handler()  → registers 34-tag SSI table.
 *           3. Calls http_set_cgi_handlers() → registers /relay.cgi route.
 *
 *         The dynamic /status.json endpoint is served by fs_open_custom()
 *         which is called automatically by LwIP when LWIP_HTTPD_CUSTOM_FILES=1.
 */
void WebServer_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */
