#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "hal/uart_types.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "parser.h"
#include "sdkconfig.h"
#include "soc/soc.h"
#include <stddef.h>
#include <string.h>

const char *WIFI_TAG = "wifi";
const char *MQTT_TAG = "mqtt";
const char *UART_TAG = "mqtt";

#define SSID CONFIG_SSID
#define PASS CONFIG_PASS
#define BROKER CONFIG_BROKER
#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group;

void wifi_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                  void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    ESP_LOGI(WIFI_TAG, "retrying connection to %s", SSID);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(WIFI_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init() {
  ESP_LOGI(WIFI_TAG, "initializing wifi");
  wifi_event_group = xEventGroupCreate();

  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_handler, NULL, &instance_got_ip));
  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = SSID,
              .password = PASS,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(WIFI_TAG, "connected to ap SSID:%s", SSID);
  } else {
    ESP_LOGE(WIFI_TAG, "unexpected event");
  }
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(wifi_event_group);
}

enum EVENTS { PUBLISH = -1000 };
ESP_EVENT_DEFINE_BASE(BASE);

void mqtt_handler(void *handler_arg, esp_event_base_t base, int32_t event_id,
                  void *event_data) {
  ESP_LOGE(MQTT_TAG, "MQTT event loop base=%s, event_id=%d", base, event_id);
  switch (event_id) {
  case MQTT_EVENT_DISCONNECTED: {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    ESP_LOGI(MQTT_TAG, "Disconnected trying to reconnect");
    // TODO: sleep for a bit?
    esp_mqtt_client_reconnect(event->client);
    break;
  }
  case MQTT_EVENT_ERROR: {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
      ESP_LOGE(MQTT_TAG, "tls_error: %s",
               esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
      ESP_LOGE(MQTT_TAG, "tls stack error: 0x%x",
               event->error_handle->esp_tls_stack_err);
    } else if (event->error_handle->error_type ==
               MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
      ESP_LOGE(MQTT_TAG, "connection refused: 0x%x",
               event->error_handle->connect_return_code);
    } else {
      ESP_LOGE(MQTT_TAG, "unknown mqtt error: 0x%x",
               event->error_handle->error_type);
    }
    break;
  }
  default:
    ESP_LOGE(MQTT_TAG, "Other mqtt event. %i", event_id);
    break;
  }
}

esp_mqtt_client_handle_t mqtt_init() {
  const esp_mqtt_client_config_t mqtt_cfg = {
      .uri = BROKER,
  };

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(
      client, MQTT_EVENT_DISCONNECTED, mqtt_handler, client));
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, MQTT_EVENT_ERROR,
                                                 mqtt_handler, client));

  ESP_ERROR_CHECK(esp_mqtt_client_start(client));
  return client;
}

void publish_handler(void *handler_arg, esp_event_base_t base, int32_t event_id,
                     void *event_data) {
  esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)handler_arg;
  switch (event_id) {
  case PUBLISH:
    ESP_LOGI(MQTT_TAG, "Publishing.");
    // TODO: figure out what to publish
    break;
  }
}

void publish_init(esp_mqtt_client_handle_t client) {
  esp_event_handler_instance_t instance_mqtt_publish;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      BASE, PUBLISH, publish_handler, client, &instance_mqtt_publish));
}

enum uart_state_t {
  START = 0,    // look for a newline
  LABEL = 1,    // parsing label
  VALUE = 2,    // parsing value
  FIELD = 3,    // have a field
  CHECKSUM = 4, // We are ready to checksum and publish the proto
};

typedef struct {
  uint8_t *data;
  size_t length;
} buffer_t;

int buffer_init(buffer_t *buf) {
  buf->data = calloc(1 + YYMAXFILL, sizeof(uint8_t));
  buf->length = 0;
  if (buf->data == NULL) {
    return ESP_ERR_NO_MEM;
  } else {
    return ESP_OK;
  }
}

void buffer_free(buffer_t *buf) { free(buf->data); }

void discard(buffer_t *buf) {}

// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html
void serial_task(void *parameters) {
  const int uart_num = UART_NUM_1;
  uart_config_t uart_config = {
      .baud_rate = 19200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };
  // Configure UART parameters
  ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
  // block and read and publish to the event loop
  enum uart_state_t state = START;
  buffer_t buffer = {0};
  ESP_ERROR_CHECK(buffer_init(&buffer));
  buffer_t label = {0};
  ESP_ERROR_CHECK(buffer_init(&label));
  buffer_t value = {0};
  ESP_ERROR_CHECK(buffer_init(&value));
  uint16_t sum = 0;
  while (true) {
    // read more if we've just started or are either in label or value mode
    if (state == START || state == LABEL || state == VALUE) {
      size_t length = 0;
      ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t *)&length));
      if (length == 0)
        continue;
      uint8_t *tmp = reallocf(buffer.data, buffer.length + length);
      if (tmp == NULL) {
        ESP_LOGE(UART_TAG, "Could not realloc %lu bytes.", length);
        buffer.length = 0;
        sum = 0;
        continue;
      }
      length =
          uart_read_bytes(uart_num, buffer.data + buffer.length, length, 100);
      if (length == 0)
        continue;
      // add what we just read to the checksum
      for (size_t i = 0; i < length; i++)
        sum += buffer.data[buffer.length + 1];
      buffer.length += length;
    }
    switch (state) {
    // Look for newline, and discard the rest of the data
    case (START): {
      uint8_t *index = memchr(buffer.data, '\n', buffer.length);
      if (index == NULL)
        continue;
      size_t off = index - buffer.data;
      memmove(buffer.data, buffer.data + off, buffer.length - off);
      buffer.length -= off;
      state = LABEL;
      break;
    }
    // Get a label for a field
    case (LABEL): {
      uint8_t *index = memchr(buffer.data, '\t', buffer.length);
      if (index == NULL)
        continue;
      size_t off = index - buffer.data;
      if (off == 0)
        continue;
      if (off == 1) {
        ESP_LOGE(UART_TAG, "empty label assuming a bad field");
        label.length = 0;
        state = START;
        continue;
      }
      off -= 1; // chomp the tab character
      label.data = reallocf(label.data, label.length + off);
      if (label.data == NULL)
        continue;
      memcpy(label.data + label.length, buffer.data, off);
      label.length = label.length + off;
      label.data[label.length] = '\0';
      // reset buffer
      buffer.length = 0;
      state = VALUE;
      break;
    }
    // Grab a value, mostly the same as label, but that is ok, more readable
    // this way.
    case (VALUE): {
      uint8_t *index = memchr(buffer.data, '\n', buffer.length);
      if (index == NULL)
        continue;
      size_t off = index - buffer.data;
      if (off == 0)
        continue;
      if (off == 1) {
        ESP_LOGE(UART_TAG, "empty value assuming a bad field");
        label.length = 0; // reset labels and values
        value.length = 0;
        state = START;
        continue;
      }
      off -= 1; // chomp the newline character
      value.data = reallocf(value.data, value.length + off);
      if (value.data == NULL)
        continue;
      memcpy(value.data + value.length, buffer.data, off);
      value.length = value.length + off;
      value.data[value.length] = '\0';
      // reset buffer
      buffer.length = 0;
      state = FIELD;
      break;
    }
    case (FIELD): {
      if (strncmp((char *)label.data, "IL", label.length) == 0) {
      }
      state = CHECKSUM;
      break;
    }
    case (CHECKSUM): {
      state = START;
      break;
    }
    }
  }
}

void serial_init() {
  xTaskCreate(serial_task, "serial_task", 1024, NULL, 10, NULL);
}

void app_main() {
  // initialize NVS -- the wifi routine uses this
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  // Initialise network
  ESP_ERROR_CHECK(esp_netif_init());
  // Create an event loop for our tasks
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init();
  esp_mqtt_client_handle_t client = mqtt_init();
  publish_init(client);
  serial_init();
}
