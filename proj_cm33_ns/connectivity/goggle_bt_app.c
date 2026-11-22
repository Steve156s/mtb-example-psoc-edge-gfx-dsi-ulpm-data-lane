/*******************************************************************************
* File Name        : goggle_bt_app.c
*
* Description      : See goggle_bt_app.h.
*
*                    Data flow
*                      Tracker --GATT write--> CM33 (this file) --shm--> CM55 HUD
*                      CM55 HUD --shm (status)--> CM33 --notify--> Tracker/phone
*
*                    All BT stack callbacks run in the BT stack task. The
*                    1 Hz link task only reads the mailbox and sends
*                    notifications.
*
*******************************************************************************/
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"

#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_uuid.h"
#include "gattdefs.h"

#include "goggle_bt_app.h"
#include "goggle_bt_cfg.h"
#include "goggle_gatt_db.h"
#include "goggle_log.h"
#include "goggle_nvm.h"
#include "goggle_wifi.h"
#include "goggle_link_proto.h"
#include "goggle_link_shm.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define LINK_TASK_NAME          ("GL Link")
#define LINK_TASK_STACK         (configMINIMAL_STACK_SIZE * 4U)
#define LINK_TASK_PRIORITY      (configMAX_PRIORITIES - 3U)
#define LINK_TASK_PERIOD_MS     (1000U)

#define MAX_KEY_SIZE            (16U)
#define CONN_ID_NONE            (0U)

/*******************************************************************************
* Types
*******************************************************************************/
typedef struct
{
    bool                        in_use;
    uint16_t                    conn_id;
    wiced_bt_device_address_t   bd_addr;
    uint8_t                     role;           /* gl_role_t */
    uint16_t                    mtu;
    uint16_t                    cccd_status;
    uint16_t                    cccd_battery;
    bool                        encrypted;
} conn_t;

/*******************************************************************************
* Globals
*******************************************************************************/
static conn_t conns[GOGGLE_BT_MAX_LINKS];
static uint8_t last_battery_pct = 0xFFU;
static uint8_t status_value[GL_STATUS_LEN];
static uint8_t battery_value = 0U;

/*******************************************************************************
* Logging into the shared-memory ring (printed by the CM55)
*******************************************************************************/
void gl_log_printf(const char* fmt, ...)
{
    char line[160];
    va_list ap;
    int n;

    n = snprintf(line, sizeof(line), "[CM33] ");
    va_start(ap, fmt);
    n += vsnprintf(&line[n], sizeof(line) - (size_t)n - 2U, fmt, ap);
    va_end(ap);

    if (n > (int)(sizeof(line) - 3U))
    {
        n = (int)(sizeof(line) - 3U);
    }
    line[n++] = '\r';
    line[n++] = '\n';
    gl_shm_log_push(line, (size_t)n);
}

/* Writer lock for the mailbox: the BT stack task, the link task and the
 * Wi-Fi task all write. The critical sections are a few hundred bytes of
 * memcpy. Before the scheduler starts (logs from main()) there is only one
 * context, and a FreeRTOS critical section there would leave BASEPRI raised
 * until the scheduler starts, so it is skipped. */
void gl_shm_port_lock(void)
{
    if (taskSCHEDULER_NOT_STARTED != xTaskGetSchedulerState())
    {
        taskENTER_CRITICAL();
    }
}

void gl_shm_port_unlock(void)
{
    if (taskSCHEDULER_NOT_STARTED != xTaskGetSchedulerState())
    {
        taskEXIT_CRITICAL();
    }
}

/*******************************************************************************
* Mailbox helpers (run inside the seqlock)
*******************************************************************************/
static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint8_t link_mask_from_conns(void)
{
    uint8_t m = 0U;

    for (uint32_t i = 0U; i < GOGGLE_BT_MAX_LINKS; i++)
    {
        if (conns[i].in_use)
        {
            /* a link that has not said HELLO yet counts as a phone */
            m |= (GL_ROLE_TRACKER == conns[i].role) ? GL_LINK_TRACKER : GL_LINK_PHONE;
        }
    }
    return m;
}

typedef struct
{
    uint8_t set;
    uint8_t clear;
} mask_ctx_t;

static void do_mask(gl_down_t* d, void* ctx)
{
    const mask_ctx_t* c = (const mask_ctx_t*)ctx;
    d->link_mask = (uint8_t)((d->link_mask & (uint8_t)~c->clear) | c->set);
}

static void shm_link_bits(uint8_t set, uint8_t clear)
{
    mask_ctx_t c = { set, clear };
    gl_shm_down_write(do_mask, &c);
}

static void shm_refresh_conn_bits(void)
{
    uint8_t m = link_mask_from_conns();
    shm_link_bits(m, (uint8_t)((GL_LINK_TRACKER | GL_LINK_PHONE) & (uint8_t)~m));
}

static void do_gnss(gl_down_t* d, void* ctx)
{
    d->gnss       = *(const gl_gnss_t*)ctx;
    d->gnss_valid = true;
    d->gnss_rx_ms = now_ms();
    if (0U != d->gnss.utc_s)
    {
        d->utc_s     = d->gnss.utc_s;
        d->utc_rx_ms = d->gnss_rx_ms;
    }
}

static void do_team(gl_down_t* d, void* ctx)
{
    d->team       = *(const gl_team_t*)ctx;
    d->team_rx_ms = now_ms();
}

static void do_msg(gl_down_t* d, void* ctx)
{
    d->msg       = *(const gl_msg_t*)ctx;
    d->msg_valid = true;
    d->msg_rx_ms = now_ms();
}

static void do_ctrl(gl_down_t* d, void* ctx)
{
    const gl_ctrl_t* c = (const gl_ctrl_t*)ctx;

    switch (c->cmd)
    {
        case GL_CMD_SET_TIME:
            d->utc_s     = c->utc_s;
            d->utc_rx_ms = now_ms();
            d->tz_q15    = c->tz_q15;
            break;
        case GL_CMD_BRIGHTNESS:
            d->brightness = c->brightness;
            break;
        case GL_CMD_CLEAR_MSG:
            d->msg_valid = false;
            break;
        default:
            break;
    }
}

static void do_passkey(gl_down_t* d, void* ctx)
{
    d->passkey = *(const uint32_t*)ctx;
    d->link_mask |= GL_LINK_PAIRING;
}

static void do_tick(gl_down_t* d, void* ctx)
{
    (void)ctx;
    d->cm33_tick_ms = now_ms();
}

/*******************************************************************************
* Connection table
*******************************************************************************/
static conn_t* conn_find(uint16_t conn_id)
{
    for (uint32_t i = 0U; i < GOGGLE_BT_MAX_LINKS; i++)
    {
        if (conns[i].in_use && (conns[i].conn_id == conn_id))
        {
            return &conns[i];
        }
    }
    return NULL;
}

static conn_t* conn_find_addr(const uint8_t* bd_addr)
{
    for (uint32_t i = 0U; i < GOGGLE_BT_MAX_LINKS; i++)
    {
        if (conns[i].in_use && (0 == memcmp(conns[i].bd_addr, bd_addr, BD_ADDR_LEN)))
        {
            return &conns[i];
        }
    }
    return NULL;
}

static uint32_t conn_count(void)
{
    uint32_t n = 0U;

    for (uint32_t i = 0U; i < GOGGLE_BT_MAX_LINKS; i++)
    {
        n += conns[i].in_use ? 1U : 0U;
    }
    return n;
}

static void adv_update(void)
{
    wiced_result_t r;
    bool want = (conn_count() < GOGGLE_BT_MAX_LINKS);

    if (want)
    {
        /* High duty for 30 s, then the stack falls back to low duty */
        r = wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,
                                          BLE_ADDR_PUBLIC, NULL);
    }
    else
    {
        r = wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, BLE_ADDR_PUBLIC, NULL);
    }
    if (WICED_SUCCESS != r)
    {
        GL_LOG("ADV %s failed 0x%x", want ? "start" : "stop", (unsigned)r);
    }
}

/*******************************************************************************
* GATT attribute values for reads
*******************************************************************************/
static wiced_bt_gatt_status_t attr_value(conn_t* c, uint16_t handle,
                                         const uint8_t** p, uint16_t* len)
{
    static uint8_t appearance[2] = { (uint8_t)(APPEARANCE_GENERIC_EYE_GLASSES & 0xFFU),
                                     (uint8_t)(APPEARANCE_GENERIC_EYE_GLASSES >> 8) };
    static uint8_t cccd[2];

    switch (handle)
    {
        case HDLC_GAP_DEVICE_NAME_VALUE:
            *p = (const uint8_t*)goggle_device_name;
            *len = (uint16_t)strlen(goggle_device_name);
            break;
        case HDLC_GAP_APPEARANCE_VALUE:
            *p = appearance;
            *len = 2U;
            break;
        case HDLC_BAS_LEVEL_VALUE:
            *p = &battery_value;
            *len = 1U;
            break;
        case HDLD_BAS_LEVEL_CCCD:
            cccd[0] = (uint8_t)(c->cccd_battery & 0xFFU);
            cccd[1] = (uint8_t)(c->cccd_battery >> 8);
            *p = cccd;
            *len = 2U;
            break;
        case HDLC_DIS_MFR_NAME_VALUE:
            *p = (const uint8_t*)goggle_dis_mfr_name;
            *len = (uint16_t)strlen(goggle_dis_mfr_name);
            break;
        case HDLC_DIS_MODEL_VALUE:
            *p = (const uint8_t*)goggle_dis_model;
            *len = (uint16_t)strlen(goggle_dis_model);
            break;
        case HDLC_DIS_FW_REV_VALUE:
            *p = (const uint8_t*)goggle_dis_fw_rev;
            *len = (uint16_t)strlen(goggle_dis_fw_rev);
            break;
        case HDLC_LINK_STATUS_VALUE:
            *p = status_value;
            *len = GL_STATUS_LEN;
            break;
        case HDLD_LINK_STATUS_CCCD:
            cccd[0] = (uint8_t)(c->cccd_status & 0xFFU);
            cccd[1] = (uint8_t)(c->cccd_status >> 8);
            *p = cccd;
            *len = 2U;
            break;
        default:
            return WICED_BT_GATT_INVALID_HANDLE;
    }
    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* GATT writes: decode and publish to the HUD
*******************************************************************************/
static const char* result_str(gl_result_t r)
{
    switch (r)
    {
        case GL_OK:          return "OK";
        case GL_ERR_LEN:     return "bad length";
        case GL_ERR_VERSION: return "bad version";
        case GL_ERR_RANGE:   return "out of range";
        default:             return "?";
    }
}

static wiced_bt_gatt_status_t write_cccd(uint16_t* cccd, const uint8_t* v, uint16_t len)
{
    if (2U != len)
    {
        return WICED_BT_GATT_INVALID_ATTR_LEN;
    }
    *cccd = (uint16_t)(v[0] | ((uint16_t)v[1] << 8));
    return WICED_BT_GATT_SUCCESS;
}

static wiced_bt_gatt_status_t handle_write(conn_t* c, uint16_t handle,
                                           const uint8_t* v, uint16_t len)
{
    gl_result_t r;

    switch (handle)
    {
        case HDLD_BAS_LEVEL_CCCD:
            return write_cccd(&c->cccd_battery, v, len);

        case HDLD_LINK_STATUS_CCCD:
            return write_cccd(&c->cccd_status, v, len);

        case HDLC_LINK_GNSS_VALUE:
        {
            gl_gnss_t g;
            r = gl_gnss_decode(v, len, &g);
            if (GL_OK != r)
            {
                break;
            }
            if (GL_ROLE_UNKNOWN == c->role)
            {
                c->role = GL_ROLE_TRACKER;
                shm_refresh_conn_bits();
            }
            gl_shm_down_write(do_gnss, &g);
            return WICED_BT_GATT_SUCCESS;
        }

        case HDLC_LINK_TEAM_VALUE:
        {
            gl_team_t t;
            r = gl_team_decode(v, len, &t);
            if (GL_OK != r)
            {
                break;
            }
            gl_shm_down_write(do_team, &t);
            return WICED_BT_GATT_SUCCESS;
        }

        case HDLC_LINK_MESSAGE_VALUE:
        {
            gl_msg_t m;
            r = gl_msg_decode(v, len, &m);
            if (GL_OK != r)
            {
                break;
            }
            GL_LOG("MSG type %u from %u: %s", (unsigned)m.type, (unsigned)m.from_id, m.text);
            gl_shm_down_write(do_msg, &m);
            return WICED_BT_GATT_SUCCESS;
        }

        case HDLC_LINK_CONTROL_VALUE:
        {
            gl_ctrl_t k;
            r = gl_ctrl_decode(v, len, &k);
            if (GL_OK != r)
            {
                break;
            }
            switch (k.cmd)
            {
                case GL_CMD_HELLO:
                    c->role = k.role;
                    GL_LOG("conn %u says HELLO as %s", (unsigned)c->conn_id,
                           (GL_ROLE_TRACKER == k.role) ? "TRACKER" :
                           ((GL_ROLE_PHONE == k.role) ? "PHONE" : "UNKNOWN"));
                    shm_refresh_conn_bits();
                    break;
                case GL_CMD_WIFI:
                    if (0U != k.wifi_on)
                    {
                        goggle_wifi_connect_saved();
                    }
                    else
                    {
                        goggle_wifi_disconnect();
                    }
                    break;
                default:
                    gl_shm_down_write(do_ctrl, &k);
                    break;
            }
            return WICED_BT_GATT_SUCCESS;
        }

        case HDLC_LINK_WIFI_CRED_VALUE:
        {
            gl_wifi_cred_t w;
            r = gl_wifi_cred_decode(v, len, &w);
            if (GL_OK != r)
            {
                break;
            }
            /* never log the password */
            GL_LOG("Wi-Fi credentials received for SSID \"%s\"", w.ssid);
            goggle_nvm_save_wifi(&w);
            goggle_wifi_connect_saved();
            return WICED_BT_GATT_SUCCESS;
        }

        default:
            return WICED_BT_GATT_WRITE_NOT_PERMIT;
    }

    GL_LOG("write 0x%04x rejected: %s", (unsigned)handle, result_str(r));
    return (GL_ERR_LEN == r) ? WICED_BT_GATT_INVALID_ATTR_LEN : WICED_BT_GATT_VALUE_NOT_ALLOWED;
}

/*******************************************************************************
* GATT request handlers
*******************************************************************************/
static void* buf_alloc(uint16_t len)
{
    return pvPortMalloc(len);
}

static void buf_free(uint8_t* p)
{
    vPortFree(p);
}

typedef void (*pfn_free_t)(uint8_t*);

static wiced_bt_gatt_status_t on_read(conn_t* c, wiced_bt_gatt_attribute_request_t* req)
{
    const uint8_t* p;
    uint16_t len;
    uint16_t offset = req->data.read_req.offset;
    wiced_bt_gatt_status_t st = attr_value(c, req->data.read_req.handle, &p, &len);

    if (WICED_BT_GATT_SUCCESS != st)
    {
        return st;
    }
    if (offset > len)
    {
        return WICED_BT_GATT_INVALID_OFFSET;
    }
    len = (uint16_t)(len - offset);
    if (len > req->len_requested)
    {
        len = req->len_requested;
    }
    /* static / long-lived buffers: no free context needed */
    return wiced_bt_gatt_server_send_read_handle_rsp(req->conn_id, req->opcode, len,
                                                     (uint8_t*)&p[offset], NULL);
}

static wiced_bt_gatt_status_t on_read_by_type(conn_t* c, wiced_bt_gatt_attribute_request_t* req)
{
    wiced_bt_gatt_read_by_type_t* rbt = &req->data.read_by_type;
    uint16_t handle = rbt->s_handle;
    uint8_t* rsp = buf_alloc(req->len_requested);
    uint8_t pair_len = 0U;
    int used = 0;

    if (NULL == rsp)
    {
        return WICED_BT_GATT_INSUF_RESOURCE;
    }

    for (;;)
    {
        const uint8_t* p;
        uint16_t len;
        int filled;

        handle = wiced_bt_gatt_find_handle_by_type(handle, rbt->e_handle, &rbt->uuid);
        if (0U == handle)
        {
            break;
        }
        if (WICED_BT_GATT_SUCCESS != attr_value(c, handle, &p, &len))
        {
            buf_free(rsp);
            return WICED_BT_GATT_READ_NOT_PERMIT;
        }
        filled = wiced_bt_gatt_put_read_by_type_rsp_in_stream(&rsp[used],
                                                              (int32_t)req->len_requested - used,
                                                              &pair_len, handle, len, p);
        if (0 == filled)
        {
            break;
        }
        used += filled;
        handle++;
    }

    if (0 == used)
    {
        buf_free(rsp);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    return wiced_bt_gatt_server_send_read_by_type_rsp(req->conn_id, req->opcode, pair_len,
                                                      (uint16_t)used, rsp,
                                                      (wiced_bt_gatt_app_context_t)buf_free);
}

static wiced_bt_gatt_status_t on_attr_request(wiced_bt_gatt_attribute_request_t* req,
                                              uint16_t* err_handle)
{
    conn_t* c = conn_find(req->conn_id);
    wiced_bt_gatt_status_t st = WICED_BT_GATT_REQ_NOT_SUPPORTED;

    if (NULL == c)
    {
        return WICED_BT_GATT_ERR_UNLIKELY;
    }

    switch (req->opcode)
    {
        case GATT_REQ_READ:
        case GATT_REQ_READ_BLOB:
            *err_handle = req->data.read_req.handle;
            st = on_read(c, req);
            break;

        case GATT_REQ_READ_BY_TYPE:
            *err_handle = req->data.read_by_type.s_handle;
            st = on_read_by_type(c, req);
            break;

        case GATT_REQ_WRITE:
        case GATT_CMD_WRITE:
            *err_handle = req->data.write_req.handle;
            if (0U != req->data.write_req.offset)
            {
                st = WICED_BT_GATT_INVALID_OFFSET;
                break;
            }
            st = handle_write(c, req->data.write_req.handle,
                              req->data.write_req.p_val, req->data.write_req.val_len);
            if ((GATT_REQ_WRITE == req->opcode) && (WICED_BT_GATT_SUCCESS == st))
            {
                (void)wiced_bt_gatt_server_send_write_rsp(req->conn_id, req->opcode,
                                                          req->data.write_req.handle);
            }
            break;

        case GATT_REQ_MTU:
            c->mtu = (req->data.remote_mtu < GOGGLE_BT_MAX_MTU) ?
                     req->data.remote_mtu : GOGGLE_BT_MAX_MTU;
            GL_LOG("conn %u MTU %u", (unsigned)c->conn_id, (unsigned)c->mtu);
            st = wiced_bt_gatt_server_send_mtu_rsp(req->conn_id, req->data.remote_mtu,
                                                   GOGGLE_BT_MAX_MTU);
            break;

        case GATT_HANDLE_VALUE_NOTIF:
            /* notification sent */
            st = WICED_BT_GATT_SUCCESS;
            break;

        default:
            /* prepared (long) writes are not supported: every packet fits
             * in one ATT write once the MTU is 247 */
            *err_handle = 0U;
            st = WICED_BT_GATT_REQ_NOT_SUPPORTED;
            break;
    }
    return st;
}

static wiced_bt_gatt_status_t on_connection(wiced_bt_gatt_connection_status_t* cs)
{
    if (cs->connected)
    {
        conn_t* c = NULL;

        for (uint32_t i = 0U; i < GOGGLE_BT_MAX_LINKS; i++)
        {
            if (!conns[i].in_use)
            {
                c = &conns[i];
                break;
            }
        }
        if (NULL == c)
        {
            /* cannot happen: ble_max_simultaneous_links = 2 */
            GL_LOG("no free connection slot");
            return WICED_BT_GATT_ERROR;
        }

        memset(c, 0, sizeof(*c));
        c->in_use  = true;
        c->conn_id = cs->conn_id;
        c->mtu     = 23U;
        memcpy(c->bd_addr, cs->bd_addr, BD_ADDR_LEN);

        GL_LOG("connected conn %u %02X:%02X:%02X:%02X:%02X:%02X", (unsigned)cs->conn_id,
               cs->bd_addr[0], cs->bd_addr[1], cs->bd_addr[2],
               cs->bd_addr[3], cs->bd_addr[4], cs->bd_addr[5]);
    }
    else
    {
        conn_t* c = conn_find(cs->conn_id);

        GL_LOG("disconnected conn %u reason 0x%02x", (unsigned)cs->conn_id, (unsigned)cs->reason);
        if (NULL != c)
        {
            c->in_use = false;
        }
        shm_link_bits(0U, GL_LINK_PAIRING);
    }

    shm_refresh_conn_bits();
    adv_update();
    return WICED_BT_GATT_SUCCESS;
}

static wiced_bt_gatt_status_t gatt_callback(wiced_bt_gatt_evt_t event,
                                            wiced_bt_gatt_event_data_t* p_data)
{
    wiced_bt_gatt_status_t st = WICED_BT_GATT_SUCCESS;

    switch (event)
    {
        case GATT_CONNECTION_STATUS_EVT:
            st = on_connection(&p_data->connection_status);
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT:
        {
            wiced_bt_gatt_attribute_request_t* req = &p_data->attribute_request;
            uint16_t err_handle = 0U;

            st = on_attr_request(req, &err_handle);
            if ((WICED_BT_GATT_SUCCESS != st) && (GATT_CMD_WRITE != req->opcode) &&
                (GATT_HANDLE_VALUE_NOTIF != req->opcode))
            {
                (void)wiced_bt_gatt_server_send_error_rsp(req->conn_id, req->opcode,
                                                          err_handle, st);
            }
            break;
        }

        case GATT_GET_RESPONSE_BUFFER_EVT:
        {
            wiced_bt_gatt_buffer_request_t* br = &p_data->buffer_request;
            br->buffer.p_app_rsp_buffer = buf_alloc(br->len_requested);
            br->buffer.p_app_ctxt = (void*)buf_free;
            break;
        }

        case GATT_APP_BUFFER_TRANSMITTED_EVT:
        {
            pfn_free_t fn = (pfn_free_t)p_data->buffer_xmitted.p_app_ctxt;
            if (NULL != fn)
            {
                fn(p_data->buffer_xmitted.p_app_data);
            }
            break;
        }

        default:
            break;
    }
    return st;
}

/*******************************************************************************
* Management callback
*******************************************************************************/
static void bt_enabled(void)
{
    wiced_bt_gatt_status_t gs;
    wiced_bt_device_address_t bda;
    wiced_bt_ble_advert_elem_t* adv;
    uint8_t n_adv;

    wiced_bt_dev_read_local_addr(bda);
    GL_LOG("BT ready, address %02X:%02X:%02X:%02X:%02X:%02X",
           bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    gs = wiced_bt_gatt_register(gatt_callback);
    if (WICED_BT_GATT_SUCCESS != gs)
    {
        GL_LOG("gatt_register failed 0x%x", (unsigned)gs);
    }

    gs = wiced_bt_gatt_db_init(goggle_gatt_db, goggle_gatt_db_len, NULL);
    if (WICED_BT_GATT_SUCCESS != gs)
    {
        GL_LOG("gatt_db_init failed 0x%x", (unsigned)gs);
    }

    /* Bonded peers (Tracker / phone) may use resolvable private addresses */
    for (uint32_t i = 0U; i < goggle_nvm_bond_count(); i++)
    {
        wiced_bt_device_link_keys_t k = *goggle_nvm_bond_at(i);
        (void)wiced_bt_dev_add_device_to_address_resolution_db(&k);
    }

    /* Allow new pairings (phone first use). For a product, close pairing
     * mode unless the user holds the button (see docs). */
    wiced_bt_set_pairable_mode(WICED_TRUE, WICED_FALSE);

    adv = goggle_bt_adv_elems(&n_adv);
    if (WICED_SUCCESS != wiced_bt_ble_set_raw_advertisement_data(n_adv, adv))
    {
        GL_LOG("set ADV data failed");
    }

    shm_link_bits(GL_LINK_BT_READY, 0U);
    adv_update();
}

static wiced_result_t bt_management_callback(wiced_bt_management_evt_t event,
                                             wiced_bt_management_evt_data_t* p_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;

    switch (event)
    {
        case BTM_ENABLED_EVT:
            if (WICED_BT_SUCCESS == p_data->enabled.status)
            {
                bt_enabled();
            }
            else
            {
                GL_LOG("BT enable failed (check the CYW55513 firmware / UART)");
            }
            break;

        case BTM_DISABLED_EVT:
            shm_link_bits(0U, GL_LINK_BT_READY | GL_LINK_ADV);
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
        {
            wiced_bt_ble_advert_mode_t mode = p_data->ble_advert_state_changed;
            if (BTM_BLE_ADVERT_OFF == mode)
            {
                shm_link_bits(0U, GL_LINK_ADV);
            }
            else
            {
                shm_link_bits(GL_LINK_ADV, 0U);
            }
            break;
        }

        case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
        {
            wiced_bt_dev_ble_io_caps_req_t* io = &p_data->pairing_io_capabilities_ble_request;
            /* The goggle has a display but no keyboard: the HUD shows the
             * passkey and the user types it on the phone (LE Secure
             * Connections, MITM protected, bonded). */
            io->local_io_cap = BTM_IO_CAPABILITIES_DISPLAY_ONLY;
            io->oob_data     = BTM_OOB_NONE;
            io->auth_req     = BTM_LE_AUTH_REQ_SC_MITM_BOND;
            io->max_key_size = MAX_KEY_SIZE;
            io->init_keys    = BTM_LE_KEY_PENC | BTM_LE_KEY_PID | BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;
            io->resp_keys    = BTM_LE_KEY_PENC | BTM_LE_KEY_PID | BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;
            break;
        }

        case BTM_PASSKEY_NOTIFICATION_EVT:
        {
            uint32_t pk = p_data->user_passkey_notification.passkey;
            GL_LOG("pairing passkey %06" PRIu32 " (shown on the HUD)", pk);
            gl_shm_down_write(do_passkey, &pk);
            break;
        }

        case BTM_SECURITY_REQUEST_EVT:
            wiced_bt_ble_security_grant(p_data->security_request.bd_addr, WICED_BT_SUCCESS);
            break;

        case BTM_PAIRING_COMPLETE_EVT:
            GL_LOG("pairing %s",
                   (WICED_SUCCESS == p_data->pairing_complete.pairing_complete_info.ble.status) ?
                   "complete" : "FAILED");
            shm_link_bits(0U, GL_LINK_PAIRING);
            break;

        case BTM_ENCRYPTION_STATUS_EVT:
        {
            conn_t* c = conn_find_addr(p_data->encryption_status.bd_addr);
            if (NULL != c)
            {
                c->encrypted = (WICED_SUCCESS == p_data->encryption_status.result);
            }
            GL_LOG("encryption %s", (WICED_SUCCESS == p_data->encryption_status.result) ?
                   "on" : "FAILED");
            break;
        }

        case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
            goggle_nvm_save_bond(&p_data->paired_device_link_keys_update);
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
            result = goggle_nvm_find_bond(p_data->paired_device_link_keys_request.bd_addr,
                                          &p_data->paired_device_link_keys_request) ?
                     WICED_BT_SUCCESS : WICED_BT_ERROR;
            break;

        case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
            goggle_nvm_save_local_keys(&p_data->local_identity_keys_update);
            break;

        case BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
            result = goggle_nvm_get_local_keys(&p_data->local_identity_keys_request) ?
                     WICED_BT_SUCCESS : WICED_BT_ERROR;
            break;

        case BTM_BLE_CONNECTION_PARAM_UPDATE:
            GL_LOG("conn params: interval %u, latency %u, timeout %u",
                   (unsigned)p_data->ble_connection_param_update.conn_interval,
                   (unsigned)p_data->ble_connection_param_update.conn_latency,
                   (unsigned)p_data->ble_connection_param_update.supervision_timeout);
            break;

        default:
            break;
    }
    return result;
}

/*******************************************************************************
* 1 Hz link task: heartbeat to the HUD, goggle status / battery notifications
*******************************************************************************/
static void notify_all(uint16_t handle, bool (*subscribed)(const conn_t*),
                       uint8_t* value, uint16_t len)
{
    for (uint32_t i = 0U; i < GOGGLE_BT_MAX_LINKS; i++)
    {
        conn_t* c = &conns[i];
        if (c->in_use && subscribed(c))
        {
            (void)wiced_bt_gatt_server_send_notification(c->conn_id, handle, len, value, NULL);
        }
    }
}

static bool sub_status(const conn_t* c)
{
    return (0U != (c->cccd_status & GATT_CLIENT_CONFIG_NOTIFICATION));
}

static bool sub_battery(const conn_t* c)
{
    return (0U != (c->cccd_battery & GATT_CLIENT_CONFIG_NOTIFICATION));
}

static void link_task(void* arg)
{
    gl_up_t up;
    (void)arg;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(LINK_TASK_PERIOD_MS));

        gl_shm_down_write(do_tick, NULL);

        if (!gl_shm_up_read(&up))
        {
            continue;
        }

        /* The goggle is the source of its own status; the link mask is
         * filled in here so the HUD core does not need to know it. */
        up.status.link_mask = link_mask_from_conns() |
                              ((GL_WIFI_CONNECTED == goggle_wifi_state()) ? GL_LINK_WIFI : 0U);
        if (GL_WIFI_CONNECTED == goggle_wifi_state())
        {
            up.status.flags |= GL_STATUS_FLAG_WIFI;
        }

        /* Sending notifications from an application task is what the
         * Infineon ESS example does too. status_value is only written here;
         * the read handler (BT stack task) may see a half-updated copy at
         * worst, which the next 1 Hz notification corrects. */
        (void)gl_status_encode(&up.status, status_value, sizeof(status_value));
        notify_all(HDLC_LINK_STATUS_VALUE, sub_status, status_value, GL_STATUS_LEN);

        if ((0xFFU != up.status.battery_pct) && (up.status.battery_pct != last_battery_pct))
        {
            last_battery_pct = up.status.battery_pct;
            battery_value = up.status.battery_pct;
            notify_all(HDLC_BAS_LEVEL_VALUE, sub_battery, &battery_value, 1U);
        }
    }
}

/*******************************************************************************
* Public
*******************************************************************************/
cy_rslt_t goggle_bt_app_start(void)
{
    wiced_result_t wr;

    /* The mailbox must be valid before the CM55 starts reading it */
    gl_shm_init_owner();
    goggle_nvm_init();

    wr = wiced_bt_stack_init(bt_management_callback, &goggle_bt_cfg_settings);
    if (WICED_BT_SUCCESS != wr)
    {
        GL_LOG("wiced_bt_stack_init failed 0x%x", (unsigned)wr);
        return CY_RSLT_TYPE_ERROR;
    }

    if (pdPASS != xTaskCreate(link_task, LINK_TASK_NAME, LINK_TASK_STACK, NULL,
                              LINK_TASK_PRIORITY, NULL))
    {
        return CY_RSLT_TYPE_ERROR;
    }
    return CY_RSLT_SUCCESS;
}
