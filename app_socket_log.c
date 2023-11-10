/* *****************************************************************************
 * File:   app_socket_log.c
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "app_socket_log.h"

#if CONFIG_APP_SOCKET_LOG_USE

#include <sdkconfig.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "drv_socket.h"
#include "app_socket.h"

#include "esp_event.h"
#include "esp_log.h"

#include "drv_console_if.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "app_socket_log"

#define USE_LOG_REDIRECT_KEEP_STDOUT    1

#define APP_SOCKET_LOG_SEND_BUFFER_SIZE  (2 * 1024)
#define APP_SOCKET_LOG_RECV_BUFFER_SIZE  256

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */

/* *****************************************************************************
 * Prototype of functions definitions for Variables
 **************************************************************************** */

/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */

#if CONFIG_SYSTEM_USE_STREAM_BUFFER
StreamBufferHandle_t log_stream_buffer_send = NULL;
StreamBufferHandle_t log_stream_buffer_recv = NULL;
#else
drv_stream_t log_stream_send = 
{
    "tx_log", NULL, 0, false, NULL, APP_SOCKET_LOG_SEND_BUFFER_SIZE,
};
drv_stream_t log_stream_recv = 
{
    "rx_log", NULL, 0, false, NULL, APP_SOCKET_LOG_RECV_BUFFER_SIZE,
};
#endif


drv_socket_t socket_log = 
{
    .cName = "log",
    .bServerType = false,
    .nSocketConnectionsCount = 0,
    .nSocketIndexPrimer = {-1},
    .nSocketIndexServer = -1,
    .cHostIP = DRV_SOCKET_DEFAULT_IP,
    .cURL = DRV_SOCKET_DEFAULT_URL,
    .u16Port = 3334,
    .bActiveTask = false,
    .bConnected = false,
    .bSendEnable = false,
    .bSendFillEnable = true,
    .bAutoSendEnable = false,
    .bIndentifyForced = true,
    .bIndentifyNeeded = false,
    .bResetSendStreamOnConnect = false,
    //.bPingUse = true,
    .bPingUse = false,
    .bLineEndingFixCRLFToCR = false,
    .bPermitBroadcast = false,
    .bConnectDeny = false,

    .bPreventOverflowReceivedData = false,
    .pTask = NULL,
    .adapter_interface = {DRV_SOCKET_IF_DEFAULT,  DRV_SOCKET_IF_BACKUP },
    .onConnect = NULL,
    .onReceive = NULL,
    .onSend = NULL,
    .onDisconnect = NULL,
    .onReceiveFrom = NULL,
    .onSendTo = NULL,
    .address_family = AF_INET,
    .protocol = IPPROTO_IP,
    .protocol_type = SOCK_STREAM,
#if CONFIG_SYSTEM_USE_STREAM_BUFFER
    .pSendStreamBuffer = {&log_stream_buffer_send},
    .pRecvStreamBuffer = {&log_stream_buffer_recv},
#else
    .pSendStream = {&log_stream_send},
    .pRecvStream = {&log_stream_recv},
#endif
};

 SemaphoreHandle_t flag_log_busy = NULL;

 int app_socket_log_send_counter = 0;

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */
int log_vprintf_stdout(const char *fmt, va_list args)
{
    size_t size_string;  
    size_string = vsnprintf(NULL, 0, fmt, args);
    if (drv_console_get_log_disabled()) return size_string;

    xSemaphoreTake(flag_log_busy, portMAX_DELAY);

    bool needed_finish_line = drv_console_is_needed_finish_line();

    if (needed_finish_line) 
    {
        va_list dummy;
        vprintf("\r\n", dummy);
    }
    vprintf(fmt, args);

    xSemaphoreGive(flag_log_busy);

    return size_string;
}

int log_vprintf(const char *fmt, va_list args)
{
    size_t size_string;  
    size_string = vsnprintf(NULL, 0, fmt, args);
    if (drv_console_get_log_disabled()) return size_string;

    xSemaphoreTake(flag_log_busy, portMAX_DELAY);

    bool needed_finish_line = drv_console_is_needed_finish_line();

    char *string;
    string = (char *)malloc(size_string+1);
    vsnprintf(string, size_string+1, fmt, args);
    size_string = strlen(string);
    if (needed_finish_line) app_socket_log_send("\r\n", 2);
    size_string = app_socket_log_send(string, size_string);

    free(string);

    #if USE_LOG_REDIRECT_KEEP_STDOUT

    if (needed_finish_line) 
    {
        va_list dummy;
        vprintf("\r\n", dummy);
    }
    vprintf(fmt, args);
    #endif

    xSemaphoreGive(flag_log_busy);

    return size_string;
}


void app_socket_log_redirect_start(void)
{
    esp_log_set_vprintf(log_vprintf);
}

void app_socket_log_redirect_stdio(void)
{
    if (flag_log_busy == NULL)
    {
        flag_log_busy = xSemaphoreCreateBinary();  
    }
    else
    {
        xSemaphoreTake(flag_log_busy, portMAX_DELAY);
    }
    xSemaphoreGive(flag_log_busy);
    esp_log_set_vprintf(log_vprintf_stdout);
}

void app_socket_log_redirect_stop(void)
{
    esp_log_set_vprintf(&vprintf);
}

void app_socket_log_init(void)
{
    if (flag_log_busy == NULL)
    {
        flag_log_busy = xSemaphoreCreateBinary();  
    }
    else
    {
        xSemaphoreTake(flag_log_busy, portMAX_DELAY);
    }
    xSemaphoreGive(flag_log_busy);
    
    #if CONFIG_SYSTEM_USE_STREAM_BUFFER
    drv_stream_buffer_init(socket_log.pSendStreamBuffer[0], APP_SOCKET_LOG_SEND_BUFFER_SIZE, "log_sock_send");
    drv_stream_buffer_init(socket_log.pRecvStreamBuffer[0], APP_SOCKET_LOG_RECV_BUFFER_SIZE, "log_sock_recv");
    #else
    drv_stream_init(socket_log.pSendStream[0], NULL, 0);
    drv_stream_init(socket_log.pRecvStream[0], NULL, 0);
    #endif
}

void app_socket_log_task(void)
{
    drv_socket_task(&socket_log, -1);
}

int app_socket_log_send(const char* pData, int size)
{
    if ((socket_log.bSendEnable) || (socket_log.bSendFillEnable))
    {
        //ESP_LOGI(TAG, "app_socket_log_send %d bytes", size);    esp log here not permitted
        //ESP_LOG_BUFFER_CHAR(TAG, pData, size);

          
        #if CONFIG_SYSTEM_USE_STREAM_BUFFER
        app_socket_log_send_counter++;
        if ((app_socket_log_send_counter % 1000) == 0)
        { 
            ESP_LOGI(TAG, "StreamBufferSend %d", app_socket_log_send_counter);
        } 
        return drv_stream_buffer_push(socket_log.pSendStreamBuffer[0], (uint8_t*)pData, size);
        #else
        return drv_stream_push(socket_log.pSendStream[0], (uint8_t*)pData, size);
        #endif


    }
    else
    {
        return 0;
    }
}

int app_socket_log_recv(char* pData, int size)
{
    if (socket_log.bConnected)
    {
        //ESP_LOGI(TAG, "app_socket_log_recv %d bytes", size);
        //ESP_LOG_BUFFER_CHAR(TAG, pData, size);
        
        #if CONFIG_SYSTEM_USE_STREAM_BUFFER
        return drv_stream_buffer_pull(socket_log.pRecvStreamBuffer[0], (uint8_t*)pData, size);
        #else
        return drv_stream_pull(socket_log.pRecvStream[0], (uint8_t*)pData, size); 
        #endif
        //return 0; 
    }
    else
    {
        return 0;
    }
}

#endif //#if CONFIG_APP_SOCKET_LOG_USE