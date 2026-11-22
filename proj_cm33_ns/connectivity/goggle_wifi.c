/*******************************************************************************
* File Name        : goggle_wifi.c
*
* Description      : See goggle_wifi.h. The SDIO bring-up follows the
*                    Infineon example mtb-example-psoc-edge-btstack-wifi-
*                    onboarding (wifi_task.c); the pins come from the BSP:
*                    KIT_PSE84_AI SDIO CLK P12.0, CMD P21.0, D0..D3 P12.1 /
*                    P12.2 / P12.4 / P12.5, WL_REG_ON P11.4.
*
*******************************************************************************/
#include <string.h>

#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"

#if !defined(GOGGLE_WIFI_ENABLE)
#define GOGGLE_WIFI_ENABLE      (1)
#endif

#if (GOGGLE_WIFI_ENABLE)
#include "cy_wcm.h"
#endif

#include "goggle_wifi.h"
#include "goggle_log.h"
#include "goggle_nvm.h"
#include "goggle_link_proto.h"
#include "goggle_link_shm.h"

#if (GOGGLE_WIFI_ENABLE)
/*******************************************************************************
* Macros
*******************************************************************************/
#define WIFI_TASK_NAME                  ("GL WiFi")
/* In words, same value as WIFI_TASK_STACK_SIZE in the Infineon example */
#define WIFI_TASK_STACK                 (4096U)
#define WIFI_TASK_PRIORITY              (2U)

#define APP_SDIO_INTERRUPT_PRIORITY     (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY (2U)
#define APP_SDIO_FREQUENCY_HZ           (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK         (64U)

#define MAX_CONNECT_RETRIES             (3U)

#define NOTIF_CONNECT                   (1UL << 0)
#define NOTIF_DISCONNECT                (1UL << 1)

/*******************************************************************************
* Globals
*******************************************************************************/
static TaskHandle_t wifi_task_handle;
static volatile gl_wifi_state_t wifi_state = GL_WIFI_OFF;

static mtb_hal_sdio_t sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;
static bool sdio_ready = false;
static bool wcm_ready = false;

/*******************************************************************************
* Mailbox update
*******************************************************************************/
typedef struct
{
    gl_wifi_state_t state;
    uint32_t        ipv4;
} wifi_shm_ctx_t;

static void do_wifi(gl_down_t* d, void* ctx)
{
    const wifi_shm_ctx_t* c = (const wifi_shm_ctx_t*)ctx;

    d->wifi_state = (uint8_t)c->state;
    d->wifi_ipv4  = c->ipv4;
    if (GL_WIFI_CONNECTED == c->state)
    {
        d->link_mask |= GL_LINK_WIFI;
    }
    else
    {
        d->link_mask &= (uint8_t)~GL_LINK_WIFI;
    }
}

static void set_state(gl_wifi_state_t st, uint32_t ipv4)
{
    wifi_shm_ctx_t c = { st, ipv4 };

    wifi_state = st;
    gl_shm_down_write(do_wifi, &c);
}

/*******************************************************************************
* SDIO bring-up (from the Infineon Wi-Fi onboarding example)
*******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

static cy_rslt_t app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;
    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc      = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };
    cy_stc_sysint_t host_wake_intr_cfg =
    {
        .intrSrc      = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&sdio_intr_cfg, sdio_interrupt_handler))
    {
        return CY_RSLT_TYPE_ERROR;
    }
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    result = mtb_hal_sdio_setup(&sdio_instance, &CYBSP_WIFI_SDIO_sdio_hal_config,
                                NULL, &sdhc_host_context);
    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW, CYBSP_WIFI_SDIO_sdio_hal_config.host_config,
                    &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);

    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size      = SDHC_SDIO_64BYTES_BLOCK;
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin, CYBSP_WIFI_WL_REG_ON_PORT_NUM,
                       CYBSP_WIFI_WL_REG_ON_PIN);
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin, CYBSP_WIFI_HOST_WAKE_PORT_NUM,
                       CYBSP_WIFI_HOST_WAKE_PIN);

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler))
    {
        return CY_RSLT_TYPE_ERROR;
    }
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Connect / disconnect
*******************************************************************************/
static void wifi_off(void)
{
    if (wcm_ready)
    {
        (void)cy_wcm_disconnect_ap();
        /* powers the WLAN core down (WL_REG_ON low); BT keeps running */
        (void)cy_wcm_deinit();
        wcm_ready = false;
    }
    set_state(GL_WIFI_OFF, 0U);
    GL_LOG("Wi-Fi off");
}

static void wifi_connect(void)
{
    gl_wifi_cred_t cred;
    cy_wcm_connect_params_t params;
    cy_wcm_ip_address_t ip;
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;

    if (!goggle_nvm_get_wifi(&cred))
    {
        GL_LOG("Wi-Fi: no credentials, provision them over BLE first");
        set_state(GL_WIFI_FAILED, 0U);
        return;
    }

    set_state(GL_WIFI_CONNECTING, 0U);

    if (!sdio_ready)
    {
        if (CY_RSLT_SUCCESS != app_sdio_init())
        {
            GL_LOG("Wi-Fi: SDIO init failed");
            set_state(GL_WIFI_FAILED, 0U);
            return;
        }
        sdio_ready = true;
    }

    if (!wcm_ready)
    {
        wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
        wcm_config.wifi_interface_instance = &sdio_instance;
        result = cy_wcm_init(&wcm_config);
        if (CY_RSLT_SUCCESS != result)
        {
            GL_LOG("Wi-Fi: cy_wcm_init failed 0x%08lx", (unsigned long)result);
            set_state(GL_WIFI_FAILED, 0U);
            return;
        }
        wcm_ready = true;
    }

    memset(&params, 0, sizeof(params));
    memcpy(params.ap_credentials.SSID, cred.ssid, cred.ssid_len);
    memcpy(params.ap_credentials.password, cred.pass, cred.pass_len);
    /* WPA2-PSK also joins WPA2/WPA3 transition networks. A WPA3-only AP
     * needs CY_WCM_SECURITY_WPA3_SAE (or a scan first, as in the Infineon
     * onboarding example). */
    params.ap_credentials.security = (0U == cred.pass_len) ? CY_WCM_SECURITY_OPEN :
                                     CY_WCM_SECURITY_WPA2_AES_PSK;

    for (uint32_t i = 0U; (i < MAX_CONNECT_RETRIES) && (CY_RSLT_SUCCESS != result); i++)
    {
        GL_LOG("Wi-Fi: joining \"%s\" (try %lu)", cred.ssid, (unsigned long)(i + 1U));
        result = cy_wcm_connect_ap(&params, &ip);
    }
    /* scrub the password from the stack */
    memset(&params, 0, sizeof(params));
    memset(&cred, 0, sizeof(cred));

    if (CY_RSLT_SUCCESS != result)
    {
        GL_LOG("Wi-Fi: join failed 0x%08lx", (unsigned long)result);
        wifi_off();
        set_state(GL_WIFI_FAILED, 0U);
        return;
    }

    /* lwIP stores IPv4 in network byte order: a.b.c.d = bytes 0..3 */
    GL_LOG("Wi-Fi: connected, IP %u.%u.%u.%u",
           (unsigned)(ip.ip.v4 & 0xFFU), (unsigned)((ip.ip.v4 >> 8) & 0xFFU),
           (unsigned)((ip.ip.v4 >> 16) & 0xFFU), (unsigned)(ip.ip.v4 >> 24));
    set_state(GL_WIFI_CONNECTED, ip.ip.v4);
}

/*******************************************************************************
* Task
*******************************************************************************/
static void wifi_task(void* arg)
{
    uint32_t notif;
    (void)arg;

    for (;;)
    {
        TickType_t wait = portMAX_DELAY;

        if ((GL_WIFI_CONNECTED == wifi_state) && (0U != GOGGLE_WIFI_AUTO_OFF_S))
        {
            wait = pdMS_TO_TICKS(GOGGLE_WIFI_AUTO_OFF_S * 1000U);
        }

        if (pdFALSE == xTaskNotifyWait(0U, UINT32_MAX, &notif, wait))
        {
            GL_LOG("Wi-Fi: auto off after %u s", (unsigned)GOGGLE_WIFI_AUTO_OFF_S);
            wifi_off();
            continue;
        }

        if (0U != (notif & NOTIF_DISCONNECT))
        {
            wifi_off();
        }
        else if (0U != (notif & NOTIF_CONNECT))
        {
            if (GL_WIFI_CONNECTED == wifi_state)
            {
                /* new credentials: rejoin */
                (void)cy_wcm_disconnect_ap();
            }
            wifi_connect();
        }
    }
}

/*******************************************************************************
* Public
*******************************************************************************/
cy_rslt_t goggle_wifi_start(void)
{
    if (pdPASS != xTaskCreate(wifi_task, WIFI_TASK_NAME, WIFI_TASK_STACK,
                              NULL, WIFI_TASK_PRIORITY, &wifi_task_handle))
    {
        return CY_RSLT_TYPE_ERROR;
    }
    return CY_RSLT_SUCCESS;
}

void goggle_wifi_connect_saved(void)
{
    if (NULL != wifi_task_handle)
    {
        (void)xTaskNotify(wifi_task_handle, NOTIF_CONNECT, eSetBits);
    }
}

void goggle_wifi_disconnect(void)
{
    if (NULL != wifi_task_handle)
    {
        (void)xTaskNotify(wifi_task_handle, NOTIF_DISCONNECT, eSetBits);
    }
}

gl_wifi_state_t goggle_wifi_state(void)
{
    return wifi_state;
}

#else /* GOGGLE_WIFI_ENABLE == 0: BLE-only build (CYW55310) */

cy_rslt_t goggle_wifi_start(void)
{
    return CY_RSLT_SUCCESS;
}

void goggle_wifi_connect_saved(void)
{
    GL_LOG("Wi-Fi not built in (GOGGLE_WIFI=0)");
}

void goggle_wifi_disconnect(void)
{
}

gl_wifi_state_t goggle_wifi_state(void)
{
    return GL_WIFI_OFF;
}

#endif /* GOGGLE_WIFI_ENABLE */
