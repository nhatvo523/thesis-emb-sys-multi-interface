/**
 * @file    web_server.c
 * @brief   LwIP Raw HTTPD web server implementation.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * SSI Tag Table  (LWIP_HTTPD_SSI_INCLUDE_TAG = 0 → tag comment is stripped,
 *                 only handler output reaches the browser)
 *
 *  Index  Tag      Output            Used in HTML as
 *  ─────  ───────  ────────────────  ────────────────────────────────────────
 *   0-7   s1r0..7  decimal uint16    <td id="s1rN"><!--s1rN--></td>
 *   8-15  s2r0..7  decimal uint16    <td id="s2rN"><!--s2rN--></td>
 *    16   s1ok     "on" | "off"      <span id="s1ok" class="dot <!--s1ok-->">
 *    17   s2ok     "on" | "off"      <span id="s2ok" class="dot <!--s2ok-->">
 *  18-21  di0..3   "on" | "off"      <span id="diN"  class="dot <!--diN-->">
 *  22-25  dit0..3  "HIGH" | "LOW"    <span id="ditN"><!--ditN--></span>
 *  26-29  rl0..3   "on" | "off"      <span id="rlN"  class="dot <!--rlN-->">
 *  30-33  rlt0..3  "ON"  | "OFF"     <span id="rltN"><!--rltN--></span>
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * CGI Route Table
 *   /relay.cgi?r=[0-3]&v=[0-1]
 *       Updates g_appData.relay[r] = v and signals BIT_RELAY_CMD.
 *       Returns /status.json so the AJAX caller receives the updated state.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * Custom Filesystem (LWIP_HTTPD_CUSTOM_FILES = 1)
 *   fs_open_custom("/status.json")
 *       Builds an HTTP/1.0 response with a JSON body containing the full
 *       g_appData snapshot.  No file on the ROM filesystem is required.
 *       All mutex-protected reads use a 50 ms timeout to keep the Ethernet
 *       task responsive even if another task holds g_dataMutex.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "web_server.h"
#include "lwip/apps/httpd.h"    /* httpd_init, http_set_ssi/cgi_handler      */
#include "lwip/apps/fs.h"       /* struct fs_file, FS_FILE_FLAGS_*           */
#include "lwip/tcpip.h"         /* tcpip_callback                            */
#include "lwip/pbuf.h"          /* struct pbuf, pbuf_free (POST callbacks)   */
#include "cmsis_os.h"           /* osMutexAcquire / Release                  */
#include "FreeRTOS.h"           /* configASSERT                              */
#include "event_groups.h"       /* xEventGroupSetBits                        */
#include <stdio.h>              /* snprintf                                   */
#include <string.h>             /* strcmp, memcpy, memset                    */
#include <stdlib.h>             /* atoi                                      */

/* MUTEX_TIMEOUT_MS is defined in app_data.h (100 ms) – used here unchanged */

/* ===========================================================================
 * SSI Tag Table
 * =========================================================================*/

static const char *ssi_tags[] =
{
    /* 0..7  – Slave 1 holding registers */
    "s1r0", "s1r1", "s1r2", "s1r3", "s1r4", "s1r5", "s1r6", "s1r7",
    /* 8..15 – Slave 2 holding registers */
    "s2r0", "s2r1", "s2r2", "s2r3", "s2r4", "s2r5", "s2r6", "s2r7",
    /* 16..17 – Slave connection status: CSS class token ("on" / "off") */
    "s1ok", "s2ok",
    /* 18..21 – Digital input CSS dot class ("on" / "off") */
    "di0", "di1", "di2", "di3",
    /* 22..25 – Digital input text label ("HIGH" / "LOW") */
    "dit0", "dit1", "dit2", "dit3",
    /* 26..29 – Relay CSS dot class ("on" / "off") */
    "rl0", "rl1", "rl2", "rl3",
    /* 30..33 – Relay text label ("ON" / "OFF") */
    "rlt0", "rlt1", "rlt2", "rlt3",
};

#define SSI_NUM_TAGS    ((int)(sizeof(ssi_tags) / sizeof(ssi_tags[0])))

/* ===========================================================================
 * SSI Handler
 *
 * Called by the HTTPD engine for every <!--tagname--> token found in .shtml
 * files. With LWIP_HTTPD_SSI_INCLUDE_TAG=0 the original comment is stripped
 * and only the pcInsert string reaches the browser.
 *
 * @param iIndex     Zero-based index into ssi_tags[].
 * @param pcInsert   Output buffer; write the replacement text here.
 * @param iInsertLen Buffer capacity (LWIP_HTTPD_MAX_TAG_INSERT_LEN = 192).
 * @return           Number of bytes written to pcInsert (≥ 0), or
 *                   HTTPD_SSI_TAG_UNKNOWN if the index is out of range.
 * ========================================================================= */
static u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen)
{
    /* Snapshot shared data under the mutex to keep lock duration minimal. */
    AppData_t snap;
    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        snap = g_appData;           /* Single struct-copy inside the lock    */
        osMutexRelease(g_dataMutex);
    }
    else
    {
        /* Mutex timeout: fall back to safe zero-value data. */
        memset(&snap, 0, sizeof(snap));
    }

    int n = 0;

    if (iIndex <= 7)
    {
        /* Tags 0-7: Slave 1 holding register values */
        n = snprintf(pcInsert, (size_t)iInsertLen, "%u",
                     (unsigned)snap.mb_slave[0].holding_reg[iIndex]);
    }
    else if (iIndex <= 15)
    {
        /* Tags 8-15: Slave 2 holding register values */
        n = snprintf(pcInsert, (size_t)iInsertLen, "%u",
                     (unsigned)snap.mb_slave[1].holding_reg[iIndex - 8]);
    }
    else if (iIndex == 16)
    {
        /* Tag 16: s1ok – CSS class token for slave 1 connection dot */
        n = snprintf(pcInsert, (size_t)iInsertLen, "%s",
                     snap.mb_slave[0].connected ? "on" : "off");
    }
    else if (iIndex == 17)
    {
        /* Tag 17: s2ok – CSS class token for slave 2 connection dot */
        n = snprintf(pcInsert, (size_t)iInsertLen, "%s",
                     snap.mb_slave[1].connected ? "on" : "off");
    }
    else if (iIndex <= 21)
    {
        /* Tags 18-21: di0..di3 – CSS dot class for digital inputs */
        uint8_t bit = (uint8_t)((snap.di_state >> (iIndex - 18)) & 0x01U);
        n = snprintf(pcInsert, (size_t)iInsertLen, "%s",
                     bit ? "on" : "off");
    }
    else if (iIndex <= 25)
    {
        /* Tags 22-25: dit0..dit3 – Digital input text label */
        uint8_t bit = (uint8_t)((snap.di_state >> (iIndex - 22)) & 0x01U);
        n = snprintf(pcInsert, (size_t)iInsertLen, "%s",
                     bit ? "HIGH" : "LOW");
    }
    else if (iIndex <= 29)
    {
        /* Tags 26-29: rl0..rl3 – CSS dot class for relay outputs */
        uint8_t bit = (uint8_t)((snap.relay_state >> (iIndex - 26)) & 0x01U);
        n = snprintf(pcInsert, (size_t)iInsertLen, "%s",
                     bit ? "on" : "off");
    }
    else if (iIndex <= 33)
    {
        /* Tags 30-33: rlt0..rlt3 – Relay text label */
        uint8_t bit = (uint8_t)((snap.relay_state >> (iIndex - 30)) & 0x01U);
        n = snprintf(pcInsert, (size_t)iInsertLen, "%s",
                     bit ? "ON" : "OFF");
    }
    else
    {
        return HTTPD_SSI_TAG_UNKNOWN;
    }

    /* Guard against snprintf truncation (n == iInsertLen means NUL was
     * dropped; n < 0 means encoding error). */
    if (n < 0)              { n = 0; }
    if (n >= iInsertLen)    { n = iInsertLen - 1; }

    return (u16_t)n;
}

/* ===========================================================================
 * CGI Handler: /relay.cgi
 *
 * URL format: GET /relay.cgi?r=[0-3]&v=[0-1]
 *
 *   r – relay index (0..3)
 *   v – desired state (0 = OFF, 1 = ON)
 *
 * On success:
 *   1. Acquires g_dataMutex and writes g_appData.relay[r] = v.
 *   2. Sets BIT_RELAY_CMD in g_systemEvents to wake the IO Task.
 *   3. Returns "/status.json" so the HTTPD serves a fresh JSON snapshot
 *      (built by fs_open_custom) as the response body.  JavaScript can
 *      therefore call fetch('/relay.cgi?...').then(r=>r.json()) and apply
 *      the returned state update immediately without a separate poll.
 *
 * Parameter bounds are validated strictly before any state modification.
 * ========================================================================= */
static const char *cgi_relay(int iIndex,
                              int iNumParams,
                              char *pcParam[],
                              char *pcValue[])
{
    (void)iIndex;

    int relay_idx = -1;
    int relay_val = -1;

    for (int i = 0; i < iNumParams; i++)
    {
        if (strcmp(pcParam[i], "r") == 0)
        {
            relay_idx = atoi(pcValue[i]);
        }
        else if (strcmp(pcParam[i], "v") == 0)
        {
            relay_val = atoi(pcValue[i]);
        }
    }

    /* Strict range check before touching shared state. */
    if ((relay_idx >= 0) && (relay_idx <= 3) &&
        ((relay_val == 0) || (relay_val == 1)))
    {
        if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
        {
            /* Write to mb_coil[] — IO task reads this to drive relay GPIO. */
            g_appData.mb_coil[relay_idx] = (uint8_t)relay_val;
            osMutexRelease(g_dataMutex);

            /* Wake IO task to latch new coil value onto GPIO.
             * Signal only after a successful write — do NOT signal when the
             * mutex times out, as mb_coil[] was not updated and waking the
             * IO task would re-apply the old (stale) relay state.
             * EVT_RELAY_CMD is separate from EVT_IO_CHANGED so the USB logger
             * is not triggered by every HTTP request. */
            xEventGroupSetBits(g_systemEvents, EVT_RELAY_CMD);
        }
    }

    /* Return the status endpoint so the AJAX caller receives the current
     * state.  Note: relay_state in the JSON still reflects the GPIO state
     * BEFORE the IO task has applied the new coil command — the update
     * is visible on the next poll or SSE push after the IO task runs. */
    return "/status.json";
}

static const tCGI cgi_handlers[] =
{
    { "/relay.cgi", cgi_relay },
};

#define CGI_NUM_HANDLERS    ((int)(sizeof(cgi_handlers) / sizeof(cgi_handlers[0])))

/* ===========================================================================
 * Custom Filesystem: /status.json  (LWIP_HTTPD_CUSTOM_FILES = 1)
 *
 * LwIP calls fs_open_custom() before searching the ROM filesystem.  This
 * hook intercepts GET /status.json and serves a dynamically built HTTP/1.0
 * response with a JSON body directly from a RAM buffer.  No file in fsdata.c
 * is required for this URL.
 *
 * Buffer layout in g_json_response[]:
 *   [  HTTP header  ][  JSON body  ]
 *    e.g. "HTTP/1.0 200 OK\r\nContent-Type: ...\r\n\r\n{...}"
 *
 * Thread safety: LwIP HTTPD runs entirely inside tcpip_thread (single-
 * threaded for the HTTP layer), so g_json_body and g_json_response are
 * never accessed concurrently.  The g_dataMutex acquisition protects the
 * read of g_appData from concurrent writes in other tasks.
 * ========================================================================= */

static char g_json_body[256];        /* JSON body only (max ~200 bytes)      */
static char g_json_response[400];    /* HTTP header (~100 B) + JSON body     */
static const char g_index_body[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "<meta charset=\"UTF-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>Industrial Gateway</title>\n"
    "<style>"
    "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,system-ui,sans-serif;background:#10131a;color:#d4d8e2}"
    "header{display:flex;justify-content:space-between;align-items:center;padding:14px 20px;background:#161b27;border-bottom:2px solid #2a3a5c}"
    "header h1{margin:0;font-size:1.1rem;color:#5bc8f5}.meta{font-size:.72rem;color:#6a7a9a}"
    "main{max-width:1080px;margin:0 auto;padding:18px 20px}.bar{margin-bottom:12px;text-align:right;font-size:.72rem;color:#6a7a9a}.bar strong{color:#3ecf8e}"
    ".card{background:#161b27;border:1px solid #2a3a5c;border-radius:8px;padding:16px 18px;margin-bottom:18px}"
    ".title{margin:0 0 14px;padding-bottom:8px;border-bottom:1px solid #2a3a5c;color:#5bc8f5;font-size:.7rem;font-weight:700;text-transform:uppercase;letter-spacing:1.2px}"
    ".slaves{display:grid;grid-template-columns:1fr 1fr;gap:14px}.panel,.io{background:#1c2236;border-radius:6px}"
    ".panel{padding:12px 14px}.ph{display:flex;align-items:center;gap:8px;margin-bottom:10px}.ph h3{margin:0;font-size:.88rem;color:#a8c4e0}"
    ".lbl{font-size:.7rem;font-weight:600;padding:1px 7px;border-radius:10px;background:#2a3a5c;color:#6a7a9a}.lbl.on{background:#153a23;color:#3ecf8e}.lbl.off{background:#3a151c;color:#e05270}"
    "table{width:100%;border-collapse:collapse;font-size:.8rem}th{background:#10131a;color:#6a7a9a;padding:5px 8px;text-align:left;font-size:.7rem;text-transform:uppercase}"
    "td{padding:4px 8px;border-bottom:1px solid #1c2236;font-family:Consolas,'Courier New',monospace;color:#e0e8ff}tr:last-child td{border-bottom:none}td:first-child{color:#6a7a9a}"
    ".ios{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.io{padding:12px 10px 10px;display:flex;flex-direction:column;align-items:center;gap:6px}"
    ".iol{font-size:.68rem;font-weight:700;color:#6a7a9a;text-transform:uppercase;letter-spacing:.8px}.iov{font-size:.8rem;font-weight:700}.btns{display:flex;gap:5px;margin-top:2px}"
    ".btn{padding:3px 11px;border:none;border-radius:4px;font-size:.72rem;font-weight:700;cursor:pointer}.onb{background:#206a3e;color:#6ef4a8;border:1px solid #3ecf8e}.offb{background:#5a1a24;color:#f47c8a;border:1px solid #e05270}"
    ".dot{display:inline-block;width:11px;height:11px;border-radius:50%}.dot.on{background:#3ecf8e;box-shadow:0 0 7px #3ecf8e88}.dot.off{background:#3a3f52}"
    "@media(max-width:640px){.slaves{grid-template-columns:1fr}.ios{grid-template-columns:repeat(2,1fr)}}"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<header><h1>Industrial Gateway Dashboard</h1><div class=\"meta\">STM32F407 • FreeRTOS • LwIP • Modbus RTU</div></header>\n"
    "<main>\n"
    "<div class=\"bar\">Last update: <strong id=\"last-update\">--</strong></div>\n"
    "<section class=\"card\"><h2 class=\"title\">Modbus Holding Registers</h2><div class=\"slaves\">"
    "<div class=\"panel\"><div class=\"ph\"><span id=\"s1ok\" class=\"dot off\"></span><h3>Slave 1</h3><span id=\"s1ok-lbl\" class=\"lbl off\">Offline</span></div><table><tr><th>Register</th><th>Value</th></tr><tr><td>HR0</td><td id=\"s1r0\">--</td></tr><tr><td>HR1</td><td id=\"s1r1\">--</td></tr><tr><td>HR2</td><td id=\"s1r2\">--</td></tr><tr><td>HR3</td><td id=\"s1r3\">--</td></tr><tr><td>HR4</td><td id=\"s1r4\">--</td></tr><tr><td>HR5</td><td id=\"s1r5\">--</td></tr><tr><td>HR6</td><td id=\"s1r6\">--</td></tr><tr><td>HR7</td><td id=\"s1r7\">--</td></tr></table></div>"
    "<div class=\"panel\"><div class=\"ph\"><span id=\"s2ok\" class=\"dot off\"></span><h3>Slave 2</h3><span id=\"s2ok-lbl\" class=\"lbl off\">Offline</span></div><table><tr><th>Register</th><th>Value</th></tr><tr><td>HR0</td><td id=\"s2r0\">--</td></tr><tr><td>HR1</td><td id=\"s2r1\">--</td></tr><tr><td>HR2</td><td id=\"s2r2\">--</td></tr><tr><td>HR3</td><td id=\"s2r3\">--</td></tr><tr><td>HR4</td><td id=\"s2r4\">--</td></tr><tr><td>HR5</td><td id=\"s2r5\">--</td></tr><tr><td>HR6</td><td id=\"s2r6\">--</td></tr><tr><td>HR7</td><td id=\"s2r7\">--</td></tr></table></div>"
    "</div></section>\n"
    "<section class=\"card\"><h2 class=\"title\">Digital Inputs</h2><div class=\"ios\">"
    "<div class=\"io\"><span class=\"iol\">DI 0</span><span id=\"di0\" class=\"dot off\"></span><span id=\"dit0\" class=\"iov\">LOW</span></div>"
    "<div class=\"io\"><span class=\"iol\">DI 1</span><span id=\"di1\" class=\"dot off\"></span><span id=\"dit1\" class=\"iov\">LOW</span></div>"
    "<div class=\"io\"><span class=\"iol\">DI 2</span><span id=\"di2\" class=\"dot off\"></span><span id=\"dit2\" class=\"iov\">LOW</span></div>"
    "<div class=\"io\"><span class=\"iol\">DI 3</span><span id=\"di3\" class=\"dot off\"></span><span id=\"dit3\" class=\"iov\">LOW</span></div>"
    "</div></section>\n"
    "<section class=\"card\"><h2 class=\"title\">Relay Outputs</h2><div class=\"ios\">"
    "<div class=\"io\"><span class=\"iol\">Relay 0</span><span id=\"rl0\" class=\"dot off\"></span><span id=\"rlt0\" class=\"iov\">OFF</span><div class=\"btns\"><button class=\"btn onb\" onclick=\"setRelay(0,1)\">ON</button><button class=\"btn offb\" onclick=\"setRelay(0,0)\">OFF</button></div></div>"
    "<div class=\"io\"><span class=\"iol\">Relay 1</span><span id=\"rl1\" class=\"dot off\"></span><span id=\"rlt1\" class=\"iov\">OFF</span><div class=\"btns\"><button class=\"btn onb\" onclick=\"setRelay(1,1)\">ON</button><button class=\"btn offb\" onclick=\"setRelay(1,0)\">OFF</button></div></div>"
    "<div class=\"io\"><span class=\"iol\">Relay 2</span><span id=\"rl2\" class=\"dot off\"></span><span id=\"rlt2\" class=\"iov\">OFF</span><div class=\"btns\"><button class=\"btn onb\" onclick=\"setRelay(2,1)\">ON</button><button class=\"btn offb\" onclick=\"setRelay(2,0)\">OFF</button></div></div>"
    "<div class=\"io\"><span class=\"iol\">Relay 3</span><span id=\"rl3\" class=\"dot off\"></span><span id=\"rlt3\" class=\"iov\">OFF</span><div class=\"btns\"><button class=\"btn onb\" onclick=\"setRelay(3,1)\">ON</button><button class=\"btn offb\" onclick=\"setRelay(3,0)\">OFF</button></div></div>"
    "</div></section>\n"
    "</main>\n"
    "<script>"
    "function applyStatus(d){var i,s1=document.getElementById('s1ok-lbl'),s2=document.getElementById('s2ok-lbl');for(i=0;i<8;i++){document.getElementById('s1r'+i).textContent=d.s1.r[i];document.getElementById('s2r'+i).textContent=d.s2.r[i];}document.getElementById('s1ok').className='dot '+(d.s1.ok?'on':'off');s1.textContent=d.s1.ok?'Connected':'Offline';s1.className='lbl '+(d.s1.ok?'on':'off');document.getElementById('s2ok').className='dot '+(d.s2.ok?'on':'off');s2.textContent=d.s2.ok?'Connected':'Offline';s2.className='lbl '+(d.s2.ok?'on':'off');for(i=0;i<4;i++){document.getElementById('di'+i).className='dot '+(d.di[i]?'on':'off');document.getElementById('dit'+i).textContent=d.di[i]?'HIGH':'LOW';document.getElementById('rl'+i).className='dot '+(d.rl[i]?'on':'off');document.getElementById('rlt'+i).textContent=d.rl[i]?'ON':'OFF';}var now=new Date();document.getElementById('last-update').textContent=now.toLocaleTimeString();}"
    "function fetchStatus(){return fetch('/status.json',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error();return r.json();}).then(applyStatus).catch(function(){document.getElementById('last-update').textContent='HTTP polling failed';});}"
    "function setRelay(idx,val){fetch('/relay.cgi?r='+idx+'&v='+val).then(function(r){return r.json();}).then(applyStatus).catch(function(){});}fetchStatus();setInterval(fetchStatus,1000);"
    "</script>\n"
    "</body>\n"
    "</html>\n";
static char g_index_response[sizeof(g_index_body) + 128];

static void web_server_setup(void *arg)
{
    (void)arg;

    httpd_init();
    http_set_ssi_handler(ssi_handler, ssi_tags, SSI_NUM_TAGS);
    http_set_cgi_handlers(cgi_handlers, CGI_NUM_HANDLERS);
}

/**
 * @brief  LwIP custom FS open hook.
 *
 * @param  file  fs_file descriptor to populate.
 * @param  name  Requested URI (e.g. "/status.json", "/index.shtml").
 * @return 1 if this hook handles the file; 0 to fall through to ROM FS.
 */
int fs_open_custom(struct fs_file *file, const char *name)
{
    if ((strcmp(name, "/") == 0) ||
        (strcmp(name, "/index.html") == 0) ||
        (strcmp(name, "/index.shtml") == 0))
    {
        int total = snprintf(
            g_index_response, sizeof(g_index_response),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %u\r\n"
            "Cache-Control: no-cache, no-store\r\n"
            "\r\n"
            "%s",
            (unsigned)(sizeof(g_index_body) - 1U),
            g_index_body
        );

        if ((total <= 0) || ((size_t)total >= sizeof(g_index_response)))
        {
            return 0;
        }

        file->data = g_index_response;
        file->len = total;
        file->index = 0;
        file->pextension = NULL;
        file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        return 1;
    }

    if (strcmp(name, "/status.json") != 0)
    {
        return 0;   /* Not our URL – let LwIP search fsdata.c. */
    }

    /* -----------------------------------------------------------------------
     * Step 1: Snapshot g_appData under mutex.
     * ---------------------------------------------------------------------- */
    AppData_t snap;
    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        snap = g_appData;
        osMutexRelease(g_dataMutex);
    }
    else
    {
        memset(&snap, 0, sizeof(snap));
    }

    /* -----------------------------------------------------------------------
     * Step 2: Build JSON body into g_json_body.
     * ---------------------------------------------------------------------- */
    int body_len = snprintf(
        g_json_body, sizeof(g_json_body),
        "{"
          "\"s1\":{\"r\":[%u,%u,%u,%u,%u,%u,%u,%u],\"ok\":%u},"
          "\"s2\":{\"r\":[%u,%u,%u,%u,%u,%u,%u,%u],\"ok\":%u},"
          "\"di\":[%u,%u,%u,%u],"
          "\"rl\":[%u,%u,%u,%u]"
        "}",
        /* Slave 1 */
        (unsigned)snap.mb_slave[0].holding_reg[0], (unsigned)snap.mb_slave[0].holding_reg[1],
        (unsigned)snap.mb_slave[0].holding_reg[2], (unsigned)snap.mb_slave[0].holding_reg[3],
        (unsigned)snap.mb_slave[0].holding_reg[4], (unsigned)snap.mb_slave[0].holding_reg[5],
        (unsigned)snap.mb_slave[0].holding_reg[6], (unsigned)snap.mb_slave[0].holding_reg[7],
        (unsigned)snap.mb_slave[0].connected,
        /* Slave 2 */
        (unsigned)snap.mb_slave[1].holding_reg[0], (unsigned)snap.mb_slave[1].holding_reg[1],
        (unsigned)snap.mb_slave[1].holding_reg[2], (unsigned)snap.mb_slave[1].holding_reg[3],
        (unsigned)snap.mb_slave[1].holding_reg[4], (unsigned)snap.mb_slave[1].holding_reg[5],
        (unsigned)snap.mb_slave[1].holding_reg[6], (unsigned)snap.mb_slave[1].holding_reg[7],
        (unsigned)snap.mb_slave[1].connected,
        /* Digital inputs — expand bitmask to 4 individual values */
        (unsigned)((snap.di_state >> 0) & 0x01U), (unsigned)((snap.di_state >> 1) & 0x01U),
        (unsigned)((snap.di_state >> 2) & 0x01U), (unsigned)((snap.di_state >> 3) & 0x01U),
        /* Relay outputs — expand bitmask to 4 individual values */
        (unsigned)((snap.relay_state >> 0) & 0x01U), (unsigned)((snap.relay_state >> 1) & 0x01U),
        (unsigned)((snap.relay_state >> 2) & 0x01U), (unsigned)((snap.relay_state >> 3) & 0x01U)
    );

    if ((body_len <= 0) || ((size_t)body_len >= sizeof(g_json_body)))
    {
        return 0;   /* snprintf error or truncation – do not serve garbage. */
    }

    /* -----------------------------------------------------------------------
     * Step 3: Build complete HTTP/1.0 response (header + body).
     *         Using HTTP/1.0 avoids mandatory Content-Length for persistent
     *         connections while keeping the implementation simple.
     * ---------------------------------------------------------------------- */
    int total = snprintf(
        g_json_response, sizeof(g_json_response),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache, no-store\r\n"
        "\r\n"
        "%s",
        body_len, g_json_body
    );

    if ((total <= 0) || ((size_t)total >= sizeof(g_json_response)))
    {
        return 0;
    }

    /* -----------------------------------------------------------------------
     * Step 4: Populate the LwIP fs_file descriptor.
     *
     * FS_FILE_FLAGS_HEADER_INCLUDED: tells the HTTPD that g_json_response
     * already contains the full HTTP response; no additional headers will
     * be prepended by the HTTPD engine.
     * ---------------------------------------------------------------------- */
    file->data       = g_json_response;
    file->len        = total;
    file->index      = 0;
    file->pextension = NULL;
    file->flags      = FS_FILE_FLAGS_HEADER_INCLUDED;

    return 1;   /* Handled. */
}

/**
 * @brief  LwIP custom FS close hook.
 *         Nothing to release for our static RAM buffer.
 */
void fs_close_custom(struct fs_file *file)
{
    (void)file;
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

void WebServer_Init(void)
{
    /* Schedule HTTPD setup in tcpip_thread, matching the raw-API threading
     * model already used by the SSE server. */
    tcpip_callback(web_server_setup, NULL);
}

/* ===========================================================================
 * HTTP POST callbacks (required by LWIP_HTTPD_SUPPORT_POST=1)
 * This server does not handle POST requests – all three callbacks return
 * "not accepted" so the HTTPD will respond with 404.
 * ========================================================================= */
err_t httpd_post_begin(void *connection, const char *uri,
                       const char *http_request, u16_t http_request_len,
                       int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
    (void)connection; (void)uri; (void)http_request; (void)http_request_len;
    (void)content_len; (void)response_uri; (void)response_uri_len; (void)post_auto_wnd;
    return ERR_ARG; /* reject all POST requests */
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    (void)connection;
    pbuf_free(p);
    return ERR_ARG;
}

void httpd_post_finished(void *connection, char *response_uri,
                         u16_t response_uri_len)
{
    (void)connection; (void)response_uri; (void)response_uri_len;
}
