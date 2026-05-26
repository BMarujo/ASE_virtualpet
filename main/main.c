#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/gpio.h"
#include "mqtt_client.h"

// Includes ADC e SD Card
#include "esp_adc/adc_oneshot.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

// Includes dos componentes locais e drivers
#include "driver_dht20.h"
#include "st7735.h"
#include "pet_assets.h"

// Definições de Pinos e Configurações (Baseados no ESP32-C6)
#define I2C_MASTER_SCL_IO           7
#define I2C_MASTER_SDA_IO           6
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100 * 1000

// SPI (Display ST7735 + SD Card)
#define PIN_MOSI                    19
#define PIN_MISO                    20  
#define PIN_CLK                     21
#define PIN_CS                      22
#define PIN_DC                      2
#define PIN_RST                     3
#define PIN_BL                      15
#define PIN_SD_CS                   18

// Botões e Extras
#define BUTTON_A_GPIO               23
#define BUTTON_B_GPIO               0
#define BUTTON_C_GPIO               4

#define LED_GPIO                    5
#define ADC_GPIO                    1
#define ADC_CHANNEL                 ADC_CHANNEL_1

/* --- WiFi Configuration --- */
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

static const char *TAG = "VirtualPet";

// --- Estruturas de Dados do Pet ---
typedef struct {
    int hunger;       // 0-100 
    int happiness;    // 0-100
    int energy;       // 0-100
    float last_temp;  
    float last_hum;   
    bool is_sleeping; 
} pet_state_t;

pet_state_t pet_state = {
    .hunger = 100,
    .happiness = 100,
    .energy = 100,
    .last_temp = 20.0,
    .last_hum = 50.0,
    .is_sleeping = false
};

SemaphoreHandle_t pet_state_mutex;
QueueHandle_t button_evt_queue;
adc_oneshot_unit_handle_t adc1_handle;

// --- Funções Auxiliares SD Card ---
void load_pet_state() {
    FILE* f = fopen("/sdcard/pet.txt", "r");
    if (f) {
        int saved_hunger = 100;
        fscanf(f, "%d", &saved_hunger);
        fclose(f);
        if(xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            pet_state.hunger = saved_hunger;
            xSemaphoreGive(pet_state_mutex);
        }
        ESP_LOGI(TAG, "Estado recuperado do SD Card: Fome = %d", saved_hunger);
    } else {
        ESP_LOGW(TAG, "Nenhum estado anterior encontrado no SD (Ficheiro novo).");
    }
}

void save_pet_state(int current_hunger) {
    FILE* f = fopen("/sdcard/pet.txt", "w");
    if (f) {
        fprintf(f, "%d\n", current_hunger);
        fclose(f);
        ESP_LOGI(TAG, "Estado guardado no SD Card! Fome = %d", current_hunger);
    } else {
        ESP_LOGE(TAG, "Falha ao abrir ficheiro no SD para guardar estado!");
    }
}

// --- Tarefas (Tasks) FreeRTOS ---
void sensor_task(void *pvParameters) {
    i2c_master_dev_handle_t* sensorHandle = (i2c_master_dev_handle_t*)pvParameters;
    while(1) {
        float temperature = 0.0f;
        float humidity = 0.0f;
        dht20_read_data_after_wait(*sensorHandle, &temperature, &humidity);
        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            pet_state.last_temp = temperature;
            pet_state.last_hum = humidity;
            xSemaphoreGive(pet_state_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void pet_logic_task(void *pvParameters) {
    uint32_t io_num;
    uint32_t wakeup_time_ms = 0; // Para controlar o "Grace Period" de 10s após acordar

    while(1) {
        int adc_raw = 0;
        esp_err_t err = adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "ADC Raw Value: %d", adc_raw);
        } else {
            ESP_LOGE(TAG, "ADC Read Error: %s", esp_err_to_name(err));
        }

        int decay_multiplier = (adc_raw / 800) + 1; // 1 a 6
        ESP_LOGI(TAG, "Decay Multiplier: %d", decay_multiplier);

        if (xQueueReceive(button_evt_queue, &io_num, pdMS_TO_TICKS(1000))) {
            if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
                if (io_num == BUTTON_A_GPIO) {
                    pet_state.hunger = (pet_state.hunger + 20 > 100) ? 100 : pet_state.hunger + 20;
                    ESP_LOGI(TAG, "Botao A: Pet Alimentado!");
                } else if (io_num == BUTTON_B_GPIO) {
                    // Botão B (Pino 0) funciona como botão de SAVE manual!
                    ESP_LOGI(TAG, "Botao B: A forçar gravação do estado no SD Card!");
                    save_pet_state(pet_state.hunger);
                }
                // O Botão C não faz nada no modo ativo, serve apenas para acordar do Light Sleep
                xSemaphoreGive(pet_state_mutex);
            }
        }
        
        bool go_to_sleep = false;
        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            pet_state.hunger = (pet_state.hunger - decay_multiplier > 0) ? pet_state.hunger - decay_multiplier : 0;
            
            if (pet_state.last_temp > 30.0) {
                pet_state.energy = (pet_state.energy > 0) ? pet_state.energy - 2 : 0;
            } else {
                pet_state.energy = (pet_state.energy > 0) ? pet_state.energy - 1 : 0;
            }

            // O LED fica desligado no estado normal
            gpio_set_level(LED_GPIO, 0);

            // Light Sleep trigger: Fome < 20. Adicionado um "Grace Period" de 10 segundos (10000ms) após o arranque/acordar!
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (pet_state.hunger < 20 && (current_time - wakeup_time_ms > 10000)) {
                 go_to_sleep = true;
                 pet_state.is_sleeping = true;
            }
            xSemaphoreGive(pet_state_mutex);
        }

        if (go_to_sleep) {
            ESP_LOGI(TAG, "Fome critica (<20)! A entrar em Light Sleep...");
            
            // Ligar o LED permanentemente durante o Light Sleep e reter o pino
            gpio_set_level(LED_GPIO, 1);
            gpio_hold_en((gpio_num_t)LED_GPIO);
            
            // Pequeno atraso para a task do ecrã atualizar o Sprite para dormir (Zzz)
            vTaskDelay(pdMS_TO_TICKS(1100));

            // Configurar wakeup pelo Botão C (GPIO 4) para o Light Sleep
            gpio_wakeup_enable((gpio_num_t)BUTTON_C_GPIO, GPIO_INTR_LOW_LEVEL);
            esp_sleep_enable_gpio_wakeup();
            
            // A CPU pára exatamente nesta linha! (O Ecrã mantém a última imagem estática e a RAM é preservada)
            esp_light_sleep_start();

            // --- ESP32 ACORDA AQUI ---
            ESP_LOGI(TAG, "Acordou do Light Sleep! (10s para alimentar antes de voltar a dormir)");
            
            gpio_hold_dis((gpio_num_t)LED_GPIO); // Libertar a retenção do pino
            
            // Desativar wakeup do botão C para evitar interrupções estranhas
            gpio_wakeup_disable((gpio_num_t)BUTTON_C_GPIO);
            
            if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
                pet_state.is_sleeping = false;
                xSemaphoreGive(pet_state_mutex);
            }
            
            gpio_set_level(LED_GPIO, 0); // Desligar LED
            wakeup_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS; // Iniciar contagem dos 10s
        }
    }
}

void display_task(void *pvParameters) {
    char temp_str[32];
    char hum_str[32];
    char status_str[32];

    while(1) {
        float temp = 0.0f;
        float hum = 0.0f;
        int fome = 0;
        bool is_sleeping = false;

        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            temp = pet_state.last_temp;
            hum = pet_state.last_hum;
            fome = pet_state.hunger;
            is_sleeping = pet_state.is_sleeping;
            xSemaphoreGive(pet_state_mutex);
        }
        
        snprintf(temp_str, sizeof(temp_str), "Temp: %.1f C", temp);
        snprintf(hum_str, sizeof(hum_str), "Hum: %.1f %%", hum);
        snprintf(status_str, sizeof(status_str), "Fome: %d ", fome);

        st7735_fill_rect(5, 5, 150, 40, ST7735_BLACK);
        st7735_draw_string(5, 5, temp_str, ST7735_WHITE, ST7735_BLACK, 1);
        st7735_draw_string(5, 20, hum_str, ST7735_WHITE, ST7735_BLACK, 1);
        st7735_draw_string(5, 35, status_str, ST7735_CYAN, ST7735_BLACK, 1);

        const uint16_t *current_sprite = pet_happy;
        if (is_sleeping) {
            current_sprite = pet_sleep;
        } else if (fome < 50) {
            current_sprite = pet_sad;
        }

        st7735_draw_image(64, 50, PET_WIDTH, PET_HEIGHT, current_sprite);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- Funções de WiFi ---
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

void wifi_telemetry_task(void *pvParameters) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://10.229.103.1:1883",
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);

    while(1) {
        float temp = 0.0f;
        float hum = 0.0f;
        int fome = 0;
        int energy = 0;
        int happiness = 0;

        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            temp = pet_state.last_temp;
            hum = pet_state.last_hum;
            fome = pet_state.hunger;
            energy = pet_state.energy;
            happiness = pet_state.happiness;
            xSemaphoreGive(pet_state_mutex);
        }

        char payload[128];
        snprintf(payload, sizeof(payload), "{\"temp\":%.1f, \"hum\":%.1f, \"hunger\":%d, \"energy\":%d, \"happiness\":%d}", temp, hum, fome, energy, happiness);
        esp_mqtt_client_publish(client, "virtualpet/status", payload, 0, 1, 0);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(button_evt_queue, &gpio_num, NULL);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "A Iniciar o Virtual Pet...");

    pet_state_mutex = xSemaphoreCreateMutex();
    button_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Config LED
    gpio_config_t led_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL<<LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    gpio_config(&led_conf);

    // Config Botões
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = (1ULL<<BUTTON_A_GPIO) | (1ULL<<BUTTON_B_GPIO) | (1ULL<<BUTTON_C_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_A_GPIO, gpio_isr_handler, (void*) BUTTON_A_GPIO);
    gpio_isr_handler_add(BUTTON_B_GPIO, gpio_isr_handler, (void*) BUTTON_B_GPIO);
    gpio_isr_handler_add(BUTTON_C_GPIO, gpio_isr_handler, (void*) BUTTON_C_GPIO);

    // ADC Init
    adc_oneshot_unit_init_cfg_t adc1InitCfg = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&adc1InitCfg, &adc1_handle);
    adc_oneshot_chan_cfg_t adcChanCfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &adcChanCfg);

    // I2C Init
    static i2c_master_bus_handle_t busHandle;
    static i2c_master_dev_handle_t dht20Handle;
    dht20_init(&busHandle, &dht20Handle, DHT20_ADDR, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);

    // Inicializar barramento SPI comum (MISO adicionado)
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    // SPI bus will be initialized, then ST7735 config will be adjusted to not re-init.
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // SD Card Init
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_CS;
    slot_config.host_id = host.slot;

    esp_err_t ret_sd = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret_sd == ESP_OK) {
        ESP_LOGI(TAG, "SD Card montado com sucesso.");
        // Restaurar estado
        load_pet_state();
    } else {
        ESP_LOGE(TAG, "Falha ao montar SD Card. Erro: %s", esp_err_to_name(ret_sd));
    }

    // TFT Init
    st7735_config_t tft_cfg = {
       .mosi_io_num = PIN_MOSI,
       .sclk_io_num = PIN_CLK,
       .cs_io_num = PIN_CS,
       .dc_io_num = PIN_DC,
       .rst_io_num = PIN_RST,
       .bl_io_num = PIN_BL,
       .host_id = SPI2_HOST
    };
    // A driver vai dar erro de estado ao iniciar o bus, mas deve adicionar o device.
    st7735_init(&tft_cfg);
    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(10, 2, "Virtual Pet IoT", ST7735_YELLOW, ST7735_BLACK, 1);

    xTaskCreate(sensor_task, "sensor_task", 4096, (void*)&dht20Handle, 5, NULL);
    xTaskCreate(pet_logic_task, "pet_logic_task", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 4096, NULL, 4, NULL);
    
    wifi_init_sta();
    xTaskCreate(wifi_telemetry_task, "wifi_telemetry_task", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "Sistema Inicializado com sucesso!");
}
