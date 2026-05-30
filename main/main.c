#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
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
#include "assets/pet_assets.h"

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

#define FOOD_BOWL_FILL_STEP       15
#define FOOD_BOWL_DRAIN_STEP      5
#define PET_EAT_THRESHOLD         5
#define PET_EAT_AMOUNT            8
#define HEALTH_SLEEP_THRESHOLD    25
#define FLIP_DURATION_MS          1200
#define FLIP_FRAME_MS             150
#define HUMIDITY_FULL_DELTA       20.0f
#define DEFAULT_HUMIDITY_BASELINE 50.0f

// --- Estruturas de Dados do Pet ---
typedef struct {
    int hunger;             // 0-100
    int happiness;          // 0-100
    int energy;             // 0-100
    int food_bowl;          // 0-100
    int food_bowl_target;   // 0-100, driven by the potentiometer
    int water_meter;        // 0-100, driven by humidity increase
    int health;             // 0-100, weighted from all other metrics
    float last_temp;
    float last_hum;
    float humidity_baseline;
    float humidity_delta;
    bool humidity_ready;
    bool is_sleeping;
    bool is_flipping;
    uint32_t flip_start_ms;
} pet_state_t;

pet_state_t pet_state = {
    .hunger = 100,
    .happiness = 100,
    .energy = 100,
    .food_bowl = 0,
    .food_bowl_target = 0,
    .water_meter = 0,
    .health = 100,
    .last_temp = 20.0,
    .last_hum = 50.0,
    .humidity_baseline = DEFAULT_HUMIDITY_BASELINE,
    .humidity_delta = 0.0f,
    .humidity_ready = false,
    .is_sleeping = false
};

SemaphoreHandle_t pet_state_mutex;
QueueHandle_t button_evt_queue;
adc_oneshot_unit_handle_t adc1_handle;
TaskHandle_t telemetry_task_handle = NULL;

static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static int percent_from_adc(int adc_raw) {
    return clamp_int((adc_raw * 100) / 4095, 0, 100);
}

static void recompute_health(pet_state_t *state) {
    int temp_penalty = 0;
    if (state->last_temp > 30.0f) {
        temp_penalty = clamp_int((int)((state->last_temp - 30.0f) * 3.0f), 0, 20);
    }

    int weighted_health =
        (state->hunger * 30) +
        (state->happiness * 20) +
        (state->energy * 20) +
        (state->water_meter * 20) +
        (state->food_bowl * 10);

    state->health = clamp_int((weighted_health / 100) - temp_penalty, 0, 100);
}

static const char *expression_for_state(const pet_state_t *state) {
    if (state->is_sleeping) return "sleeping";
    if (state->is_flipping) return "flipping";
    if (state->energy < 30) return "tired";
    if (state->water_meter < 20) return "thirsty";
    if (state->hunger < 35) return "hungry";
    if (state->happiness > 75 && state->health > 60) return "happy";
    return "idle";
}

static const uint16_t *sprite_for_state(const pet_state_t *state, uint32_t now_ms) {
    if (state->is_flipping) {
        uint32_t elapsed = now_ms - state->flip_start_ms;
        uint32_t frame = (elapsed / FLIP_FRAME_MS) % CAT_FLIP_FRAME_COUNT;
        return cat_flip_frames[frame];
    }

    if (state->is_sleeping) return cat_sleep;
    if (state->energy < 30) return cat_tired;
    if (state->water_meter < 20) return cat_thirsty;
    if (state->hunger < 35) return cat_hungry;
    if (state->happiness > 75 && state->health > 60) return cat_happy;
    return cat_idle;
}

static const uint16_t *bowl_sprite_for_fill(int fill) {
    int frame = clamp_int((fill + 12) / 25, 0, BOWL_FRAME_COUNT - 1);
    return food_bowl_frames[frame];
}

// --- Funções Auxiliares SD Card ---
void load_pet_state(void) {
    FILE* f = fopen("/sdcard/pet.txt", "r");
    if (f) {
        pet_state_t loaded = pet_state;
        char line[96];
        bool parsed_versioned_state = false;

        if (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "VP2", 3) == 0) {
                parsed_versioned_state = true;
                while (fgets(line, sizeof(line), f)) {
                    char key[32];
                    char value[32];
                    if (sscanf(line, "%31[^=]=%31s", key, value) != 2) {
                        continue;
                    }

                    if (strcmp(key, "hunger") == 0) loaded.hunger = atoi(value);
                    else if (strcmp(key, "happiness") == 0) loaded.happiness = atoi(value);
                    else if (strcmp(key, "energy") == 0) loaded.energy = atoi(value);
                    else if (strcmp(key, "food_bowl") == 0) loaded.food_bowl = atoi(value);
                    else if (strcmp(key, "food_bowl_target") == 0) loaded.food_bowl_target = atoi(value);
                    else if (strcmp(key, "water_meter") == 0) loaded.water_meter = atoi(value);
                    else if (strcmp(key, "health") == 0) loaded.health = atoi(value);
                    else if (strcmp(key, "last_temp") == 0) loaded.last_temp = strtof(value, NULL);
                    else if (strcmp(key, "last_hum") == 0) loaded.last_hum = strtof(value, NULL);
                    else if (strcmp(key, "humidity_baseline") == 0) loaded.humidity_baseline = strtof(value, NULL);
                    else if (strcmp(key, "humidity_delta") == 0) loaded.humidity_delta = strtof(value, NULL);
                    else if (strcmp(key, "humidity_ready") == 0) loaded.humidity_ready = atoi(value) != 0;
                }
            } else {
                loaded.hunger = atoi(line);
                ESP_LOGW(TAG, "Estado antigo encontrado no SD; migrei apenas a fome.");
            }
        }
        fclose(f);

        loaded.hunger = clamp_int(loaded.hunger, 0, 100);
        loaded.happiness = clamp_int(loaded.happiness, 0, 100);
        loaded.energy = clamp_int(loaded.energy, 0, 100);
        loaded.food_bowl = clamp_int(loaded.food_bowl, 0, 100);
        loaded.food_bowl_target = clamp_int(loaded.food_bowl_target, 0, 100);
        loaded.water_meter = clamp_int(loaded.water_meter, 0, 100);
        loaded.health = clamp_int(loaded.health, 0, 100);
        loaded.is_sleeping = false;
        loaded.is_flipping = false;
        loaded.flip_start_ms = 0;
        loaded.humidity_ready = false; // Força uma nova calibração térmica inicial no arranque
        recompute_health(&loaded);

        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            pet_state = loaded;
            xSemaphoreGive(pet_state_mutex);
        }

        if (parsed_versioned_state) {
            ESP_LOGI(TAG, "Estado recuperado do SD Card: fome=%d bowl=%d water=%d health=%d",
                     loaded.hunger, loaded.food_bowl, loaded.water_meter, loaded.health);
        }
    } else {
        ESP_LOGW(TAG, "Nenhum estado anterior encontrado no SD (Ficheiro novo).");
    }
}

void save_pet_state(const pet_state_t *snapshot) {
    FILE* f = fopen("/sdcard/pet.txt", "w");
    if (f) {
        fprintf(f, "VP2\n");
        fprintf(f, "hunger=%d\n", snapshot->hunger);
        fprintf(f, "happiness=%d\n", snapshot->happiness);
        fprintf(f, "energy=%d\n", snapshot->energy);
        fprintf(f, "food_bowl=%d\n", snapshot->food_bowl);
        fprintf(f, "food_bowl_target=%d\n", snapshot->food_bowl_target);
        fprintf(f, "water_meter=%d\n", snapshot->water_meter);
        fprintf(f, "health=%d\n", snapshot->health);
        fprintf(f, "last_temp=%.2f\n", snapshot->last_temp);
        fprintf(f, "last_hum=%.2f\n", snapshot->last_hum);
        fprintf(f, "humidity_baseline=%.2f\n", snapshot->humidity_baseline);
        fprintf(f, "humidity_delta=%.2f\n", snapshot->humidity_delta);
        fprintf(f, "humidity_ready=%d\n", snapshot->humidity_ready ? 1 : 0);
        fclose(f);
        ESP_LOGI(TAG, "Estado guardado no SD Card! fome=%d energia=%d feliz=%d agua=%d tigela=%d hp=%d",
                 snapshot->hunger, snapshot->energy, snapshot->happiness, snapshot->water_meter, snapshot->food_bowl, snapshot->health);
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

            if (!pet_state.humidity_ready) {
                pet_state.last_hum = humidity;
                pet_state.humidity_delta = 0.0f;
                pet_state.humidity_ready = true;
            } else {
                float diff = humidity - pet_state.last_hum;
                if (diff > 0.5f) { // Soprou para o sensor (aumento)
                    int water_boost = (int)(diff * 3.0f); // 1% de humidade = 3% de água
                    pet_state.water_meter = clamp_int(pet_state.water_meter + water_boost, 0, 100);
                    pet_state.humidity_delta = diff;
                } else {
                    pet_state.humidity_delta = 0.0f;
                }
                pet_state.last_hum = humidity; // Atualiza a "memória" para o próximo ciclo
            }

            recompute_health(&pet_state);
            xSemaphoreGive(pet_state_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void pet_logic_task(void *pvParameters) {
    uint32_t io_num;
    uint32_t wakeup_time_ms = 0; // Para controlar o "Grace Period" de 10s após acordar
    uint32_t last_pet_tick_ms = 0;
    uint32_t last_button_a_ms = 0;
    uint32_t last_button_b_ms = 0;
    uint32_t last_button_c_ms = 0;

    while(1) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        int adc_raw = 0;
        esp_err_t err = adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw);
        if (err == ESP_OK) {
            ESP_LOGD(TAG, "ADC Raw Value: %d", adc_raw);
        } else {
            ESP_LOGE(TAG, "ADC Read Error: %s", esp_err_to_name(err));
        }

        int bowl_target = percent_from_adc(adc_raw);
        bool save_requested = false;
        pet_state_t save_snapshot = {0};

        while (xQueueReceive(button_evt_queue, &io_num, 0)) {
            if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
                if (io_num == BUTTON_A_GPIO) {
                    if ((now_ms - last_button_a_ms) > 250 && !pet_state.is_sleeping) {
                        pet_state.is_flipping = true;
                        pet_state.flip_start_ms = now_ms;
                        pet_state.happiness = clamp_int(pet_state.happiness + 12, 0, 100);
                        pet_state.energy = clamp_int(pet_state.energy - 5, 0, 100);
                        pet_state.hunger = clamp_int(pet_state.hunger - 1, 0, 100);
                        recompute_health(&pet_state);
                        ESP_LOGI(TAG, "Botao A: o gato fez um flip!");
                        last_button_a_ms = now_ms;
                    }
                } else if (io_num == BUTTON_B_GPIO) {
                    if ((now_ms - last_button_b_ms) > 250) {
                        save_snapshot = pet_state;
                        save_requested = true;
                        ESP_LOGI(TAG, "Botao B: a guardar estado completo no SD Card!");
                        last_button_b_ms = now_ms;
                    }
                } else if (io_num == BUTTON_C_GPIO) {
                    if ((now_ms - last_button_c_ms) > 250) {
                        ESP_LOGD(TAG, "Botao C ativo; reservado para acordar do Light Sleep.");
                        last_button_c_ms = now_ms;
                    }
                }
                xSemaphoreGive(pet_state_mutex);
            }
        }

        if (save_requested) {
            save_pet_state(&save_snapshot);
        }
        
        bool go_to_sleep = false;
        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            pet_state.food_bowl_target = bowl_target;
            if (pet_state.food_bowl < pet_state.food_bowl_target) {
                pet_state.food_bowl = clamp_int(pet_state.food_bowl + FOOD_BOWL_FILL_STEP, 0, pet_state.food_bowl_target);
            } else if (pet_state.food_bowl > pet_state.food_bowl_target) {
                pet_state.food_bowl = clamp_int(pet_state.food_bowl - FOOD_BOWL_DRAIN_STEP, pet_state.food_bowl_target, 100);
            }

            if (pet_state.is_flipping && (now_ms - pet_state.flip_start_ms) >= FLIP_DURATION_MS) {
                pet_state.is_flipping = false;
            }

            if ((now_ms - last_pet_tick_ms) >= 2000) {
                if (pet_state.food_bowl >= PET_EAT_THRESHOLD && pet_state.hunger < 100) {
                    int eat_amount = (pet_state.food_bowl >= 60) ? PET_EAT_AMOUNT + 4 : PET_EAT_AMOUNT;
                    eat_amount = clamp_int(eat_amount, 0, pet_state.food_bowl);
                    pet_state.food_bowl = clamp_int(pet_state.food_bowl - eat_amount, 0, 100);
                    pet_state.hunger = clamp_int(pet_state.hunger + eat_amount, 0, 100);
                    pet_state.happiness = clamp_int(pet_state.happiness + 3, 0, 100);
                } else {
                    pet_state.hunger = clamp_int(pet_state.hunger - 3, 0, 100);
                }
                
                // Decaimento natural acelerado da água
                pet_state.water_meter = clamp_int(pet_state.water_meter - 3, 0, 100);

                int energy_drop = (pet_state.last_temp > 30.0f) ? 6 : 3;
                if (!pet_state.is_flipping && pet_state.hunger > 55 && pet_state.water_meter > 35) {
                    pet_state.energy = clamp_int(pet_state.energy + 5, 0, 100);
                } else {
                    pet_state.energy = clamp_int(pet_state.energy - energy_drop, 0, 100);
                }

                if (pet_state.hunger < 30 || pet_state.water_meter < 15 || pet_state.energy < 20) {
                    pet_state.happiness = clamp_int(pet_state.happiness - 2, 0, 100);
                } else if (pet_state.food_bowl > 40 && pet_state.health > 55) {
                    pet_state.happiness = clamp_int(pet_state.happiness + 1, 0, 100);
                }
                last_pet_tick_ms = now_ms;
            }

            recompute_health(&pet_state);
            gpio_set_level(LED_GPIO, 0);

            if (pet_state.health < HEALTH_SLEEP_THRESHOLD && (now_ms - wakeup_time_ms > 20000)) {
                 go_to_sleep = true;
                 pet_state.is_sleeping = true;
                 pet_state.is_flipping = false;
            }
            xSemaphoreGive(pet_state_mutex);
        }

        if (go_to_sleep) {
            ESP_LOGI(TAG, "Saude critica (<%d)! A entrar em Light Sleep...", HEALTH_SLEEP_THRESHOLD);
            
            // Ligar o LED permanentemente indicando transição para sleep
            gpio_set_level(LED_GPIO, 1);
            gpio_hold_en((gpio_num_t)LED_GPIO);
            
            // Atraso de 3 segundos para garantir duas coisas:
            // 1. O utilizador vê o sprite "Zzz" com clareza antes do ecrã apagar.
            // 2. O RTOS e a stack TCP/IP do Wi-Fi têm tempo de sobra para efetuar o "flush" completo do pacote MQTT "is_sleeping:true"
            vTaskDelay(pdMS_TO_TICKS(3000));

            // Desligar o Ecrã e fixar o pino em baixo durante o sono profundo
            gpio_set_level(PIN_BL, 0);
            gpio_hold_en((gpio_num_t)PIN_BL);

            // Configurar wakeup pelo Botão C (GPIO 4) para o Light Sleep
            gpio_wakeup_enable((gpio_num_t)BUTTON_C_GPIO, GPIO_INTR_LOW_LEVEL);
            esp_sleep_enable_gpio_wakeup();
            
            // A CPU pára exatamente nesta linha! (O Ecrã mantém a última imagem estática e a RAM é preservada)
            esp_light_sleep_start();

            // --- ESP32 ACORDA AQUI ---
            ESP_LOGI(TAG, "Acordou do Light Sleep! (20s para alimentar antes de voltar a dormir)");
            
            // Limpar a fila de eventos de botões para ignorar ruído elétrico/glitches gerados ao adormecer ou acordar
            xQueueReset(button_evt_queue);
            
            gpio_hold_dis((gpio_num_t)LED_GPIO); // Libertar a retenção do pino
            gpio_hold_dis((gpio_num_t)PIN_BL);
            
            // Desativar wakeup do botão C para evitar interrupções estranhas
            gpio_wakeup_disable((gpio_num_t)BUTTON_C_GPIO);
            
            if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
                pet_state.is_sleeping = false;
                xSemaphoreGive(pet_state_mutex);
            }
            
            gpio_set_level(LED_GPIO, 0); // Desligar LED
            gpio_set_level(PIN_BL, 1);
            wakeup_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS; // Iniciar contagem dos 20s
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static uint16_t health_bar_color(int health) {
    if (health < HEALTH_SLEEP_THRESHOLD) return ST7735_RED;
    if (health < 60) return ST7735_YELLOW;
    return ST7735_GREEN;
}

static void draw_labeled_bar(uint16_t x, uint16_t y, const char *label, uint16_t bar_width, int percent, uint16_t fill_color) {
    uint16_t label_width = strlen(label) * 6;
    uint16_t bar_x = x + label_width + 2;
    int fill_width = ((bar_width - 2) * clamp_int(percent, 0, 100)) / 100;

    st7735_draw_string(x, y, label, ST7735_WHITE, ST7735_BLACK, 1);
    st7735_fill_rect(bar_x, y, bar_width, 6, ST7735_GRAY);
    st7735_fill_rect(bar_x + 1, y + 1, bar_width - 2, 4, ST7735_BLACK);
    if (fill_width > 0) {
        st7735_fill_rect(bar_x + 1, y + 1, fill_width, 4, fill_color);
    }
}

static void draw_changed_text(uint16_t x, uint16_t y, uint16_t w, const char *text,
                              char *previous, size_t previous_size,
                              uint16_t color) {
    if (strncmp(previous, text, previous_size) == 0) {
        return;
    }

    st7735_fill_rect(x, y, w, 8, ST7735_BLACK);
    st7735_draw_string(x, y, text, color, ST7735_BLACK, 1);
    strlcpy(previous, text, previous_size);
}

void display_task(void *pvParameters) {
    char status_str[40];
    char sensor_str[40];
    bool first_frame = true;
    char previous_status[40] = "";
    char previous_sensor[40] = "";
    char previous_expression[16] = "";
    int previous_health = -1;
    int previous_water = -1;
    int previous_hunger = -1;
    int previous_energy = -1;
    const uint16_t *previous_sprite = NULL;
    const uint16_t *previous_bowl_sprite = NULL;

    while(1) {
        pet_state_t snapshot;

        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            snapshot = pet_state;
            xSemaphoreGive(pet_state_mutex);
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        const uint16_t *current_sprite = sprite_for_state(&snapshot, now_ms);
        const uint16_t *bowl_sprite = bowl_sprite_for_fill(snapshot.food_bowl);
        const char *expression = expression_for_state(&snapshot);

        snprintf(status_str, sizeof(status_str), "Bowl:%3d%% Mood:%3d", snapshot.food_bowl, snapshot.happiness);
        snprintf(sensor_str, sizeof(sensor_str), "T:%.1fC H:%.1f%% d:%.1f", snapshot.last_temp, snapshot.last_hum, snapshot.humidity_delta);

        if (first_frame) {
            st7735_fill_screen(ST7735_BLACK);
            first_frame = false;
        }

        if (snapshot.health != previous_health) {
            draw_labeled_bar(2, 2, "HP", 54, snapshot.health, health_bar_color(snapshot.health));
            previous_health = snapshot.health;
        }
        if (snapshot.water_meter != previous_water) {
            draw_labeled_bar(82, 2, "H2O", 50, snapshot.water_meter, ST7735_CYAN);
            previous_water = snapshot.water_meter;
        }
        if (snapshot.hunger != previous_hunger) {
            draw_labeled_bar(2, 11, "F", 60, snapshot.hunger, ST7735_ORANGE);
            previous_hunger = snapshot.hunger;
        }
        if (snapshot.energy != previous_energy) {
            draw_labeled_bar(82, 11, "E", 60, snapshot.energy, ST7735_MAGENTA);
            previous_energy = snapshot.energy;
        }

        draw_changed_text(2, 22, 154, status_str, previous_status, sizeof(previous_status), ST7735_WHITE);
        draw_changed_text(2, 72, 154, sensor_str, previous_sensor, sizeof(previous_sensor), ST7735_GRAY);
        draw_changed_text(106, 38, 54, expression, previous_expression, sizeof(previous_expression), ST7735_GRAY);

        if (current_sprite != previous_sprite) {
            st7735_draw_image(18, 39, CAT_SPRITE_WIDTH, CAT_SPRITE_HEIGHT, current_sprite);
            previous_sprite = current_sprite;
        }
        if (bowl_sprite != previous_bowl_sprite) {
            st7735_draw_image(108, 50, BOWL_SPRITE_WIDTH, BOWL_SPRITE_HEIGHT, bowl_sprite);
            previous_bowl_sprite = bowl_sprite;
        }

        vTaskDelay(pdMS_TO_TICKS(snapshot.is_flipping ? FLIP_FRAME_MS : 1000));
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

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        esp_mqtt_client_subscribe(event->client, "virtualpet/command", 0);
        ESP_LOGI(TAG, "MQTT subscrito: virtualpet/command");
    } else if (event_id == MQTT_EVENT_DATA) {
        if (event->topic_len == 18 && strncmp(event->topic, "virtualpet/command", 18) == 0) {
            char payload[64];
            int len = event->data_len < 63 ? event->data_len : 63;
            memcpy(payload, event->data, len);
            payload[len] = '\0';
            if (strstr(payload, "play") != NULL) {
                uint32_t io_num = BUTTON_A_GPIO;
                xQueueSend(button_evt_queue, &io_num, 0);
                ESP_LOGI(TAG, "Comando MQTT: PLAY! Simulando Botao A.");
            }
        }
    }
}

void wifi_telemetry_task(void *pvParameters) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://10.229.103.1:1883",
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    while(1) {
        pet_state_t snapshot;

        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            snapshot = pet_state;
            xSemaphoreGive(pet_state_mutex);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        char payload[320];
        snprintf(payload, sizeof(payload),
                 "{\"temp\":%.1f,\"hum\":%.1f,\"humidity_delta\":%.1f,"
                 "\"hunger\":%d,\"energy\":%d,\"happiness\":%d,"
                 "\"food_bowl\":%d,\"food_bowl_target\":%d,"
                 "\"water_meter\":%d,\"health\":%d,"
                 "\"is_sleeping\":%s,\"is_flipping\":%s,\"expression\":\"%s\"}",
                 snapshot.last_temp, snapshot.last_hum, snapshot.humidity_delta,
                 snapshot.hunger, snapshot.energy, snapshot.happiness,
                 snapshot.food_bowl, snapshot.food_bowl_target,
                 snapshot.water_meter, snapshot.health,
                 snapshot.is_sleeping ? "true" : "false",
                 snapshot.is_flipping ? "true" : "false",
                 expression_for_state(&snapshot));
        ESP_LOGI(TAG, "MQTT status: %s", payload);
        esp_mqtt_client_publish(client, "virtualpet/status", payload, 0, 1, 0);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
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
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

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
       .host_id = SPI2_HOST,
       .skip_bus_init = true
    };
    st7735_init(&tft_cfg);
    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(10, 2, "Virtual Cat IoT", ST7735_YELLOW, ST7735_BLACK, 1);

    xTaskCreate(sensor_task, "sensor_task", 4096, (void*)&dht20Handle, 5, NULL);
    xTaskCreate(pet_logic_task, "pet_logic_task", 8192, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 8192, NULL, 4, NULL);
    
    wifi_init_sta();
    xTaskCreate(wifi_telemetry_task, "wifi_telemetry_task", 8192, NULL, 4, &telemetry_task_handle);
    
    ESP_LOGI(TAG, "Sistema Inicializado com sucesso!");
}
