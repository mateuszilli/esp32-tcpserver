#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <ultrasonic.h>
#include <dht.h>


#ifdef CONFIG_EXAMPLE_IPV4
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#else
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV6_ADDR
#endif

#define PORT CONFIG_EXAMPLE_PORT
#define MAX_DISTANCE_CM 500
#define DHT         GPIO_NUM_14
#define TRIGGER     GPIO_NUM_12
#define ECHO        GPIO_NUM_13

QueueHandle_t bufferTemperature;
QueueHandle_t bufferDistance;
QueueHandle_t bufferHumidity;

static const char *TAG = "TCPSERVER";

void task_ultrasonic(void *pvParamters);
void task_dht(void *pvParameters);
static void task_tcpserver(void *pvParameters);
static void do_retransmit(const int sock);

void task_ultrasonic(void *pvParamters) {
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER,
        .echo_pin = ECHO
    };

    ultrasonic_init(&sensor);

    while (1) {
        uint32_t distance = 0;
        esp_err_t res = ultrasonic_measure_cm(&sensor, MAX_DISTANCE_CM, &distance);
        if (res != ESP_OK) {

            switch (res) {
                case ESP_ERR_ULTRASONIC_PING:
                    ESP_LOGE(TAG, "ERROR - Cannot ping (device is in invalid state)");
                    break;
                case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                    ESP_LOGE(TAG, "ERROR - Ping timeout (no device found)");
                    break;
                case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                    ESP_LOGE(TAG, "ERROR - Echo timeout (i.e. distance too big)");
                    break;
                default:
                    ESP_LOGE(TAG, "ERROR - %d", res);
            }
        }

        ESP_LOGE(TAG, "Distance: %d cm", distance);
        xQueueSend(bufferDistance, &distance, pdMS_TO_TICKS(0));

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void task_dht(void *pvParameters) {
    int16_t temperature = 0;
    int16_t humidity = 0;

    while (1) {
        if (dht_read_data(DHT_TYPE_DHT11, DHT, &humidity, &temperature) == ESP_OK) {
            humidity = humidity / 10;
            temperature = temperature / 10;
            ESP_LOGE(TAG, "Humidity: %d%% Temp: %dC", humidity, temperature);
            xQueueSend(bufferTemperature, &temperature, pdMS_TO_TICKS(0));
			xQueueSend(bufferHumidity, &humidity, pdMS_TO_TICKS(0));
        } else {
            ESP_LOGE(TAG, "Could not read data from sensor");
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

static void task_tcpserver(void *pvParameters) {
    char addr_str[128];
    int addr_family;
    int ip_protocol;


#ifdef CONFIG_EXAMPLE_IPV4
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else
    struct sockaddr_in6 dest_addr;
    bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(PORT);
    addr_family = AF_INET6;
    ip_protocol = IPPROTO_IPV6;
    inet6_ntoa_r(dest_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr;
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        if (source_addr.sin6_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.sin6_family == PF_INET6) {
            inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

static void do_retransmit(const int sock) {
    int len;
    char rx_buffer[128];
	char default_msg[] = "Digite um comando valido (T - Temperatura, H - Humidade, D - Distancia)\n";
	uint16_t temp;
	char stringTemperatura[10];
	uint16_t dist;
	char stringDistancia[10];
	uint16_t humd;
	char stringHumidade[10];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0;
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

			switch (rx_buffer[0]) {
				case 'T':
					xQueueReceive(bufferTemperature, &temp, pdMS_TO_TICKS(2000));
					sprintf(stringTemperatura, "%d C\n", temp);
				    send(sock, stringTemperatura, strlen(stringTemperatura), 0);
				break;

				case 'H':
				    xQueueReceive(bufferHumidity, &humd, pdMS_TO_TICKS(2000));
					sprintf(stringHumidade, "%d%% \n", humd);
				    send(sock, stringHumidade, strlen(stringHumidade), 0);
				break;

				case 'D':
				    xQueueReceive(bufferDistance, &dist, pdMS_TO_TICKS(2000));
					sprintf(stringDistancia, "%d cm\n", dist);
				    send(sock, stringDistancia, strlen(stringDistancia), 0);
				break;

				default:
					send(sock, default_msg, strlen(default_msg), 0);
			}
        }
    } while (len > 0);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

	gpio_pad_select_gpio(DHT);
    gpio_set_direction(DHT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DHT, GPIO_PULLUP_ONLY);

    gpio_pad_select_gpio(TRIGGER);
    gpio_set_direction(TRIGGER, GPIO_MODE_INPUT);
    gpio_pad_select_gpio(ECHO);
    gpio_set_direction(ECHO, GPIO_MODE_INPUT);

    bufferTemperature = xQueueCreate(5, sizeof(uint16_t));
    bufferDistance = xQueueCreate(5, sizeof(uint16_t));
	bufferHumidity = xQueueCreate(5, sizeof(uint16_t));

    xTaskCreate(task_ultrasonic, "task_ultrasonic", configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL);
    xTaskCreate(task_dht, "task_dht", configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL);
    xTaskCreate(task_tcpserver, "task_tcpserver", configMINIMAL_STACK_SIZE * 5, NULL, 2, NULL);
}
