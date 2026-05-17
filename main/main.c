#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

// Includes dos componentes locais e drivers
#include "driver_dht20.h"

// Definições de Pinos e Configurações (Baseados no ESP32-C6)
// I2C (Sensor DHT20)
#define I2C_MASTER_SCL_IO           7
#define I2C_MASTER_SDA_IO           6
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100 * 1000

// SPI (Display ST7735)
#define PIN_MOSI                    19
#define PIN_MISO                    20  // Não usado
#define PIN_CLK                     21
#define PIN_CS                      22
#define PIN_DC                      2
#define PIN_RST                     3
#define PIN_BL                      15

// Botões e Extras
#define BUTTON_A_GPIO               23
#define BUTTON_B_GPIO               9
#define BUTTON_C_GPIO               4

#define LED_GPIO                    5
#define ADC_GPIO                    11

static const char *TAG = "VirtualPet";

// --- Estruturas de Dados do Pet ---
typedef struct {
    int hunger;       // 0-100 (0 = fome máxima, 100 = cheio)
    int happiness;    // 0-100
    int energy;       // 0-100
    float last_temp;  // Temperatura lida pelo DHT20
    float last_hum;   // Humidade lida pelo DHT20
    bool is_sleeping; // Estado de sono
} pet_state_t;

// Estado global do Pet partilhado entre tasks
pet_state_t pet_state = {
    .hunger = 100,
    .happiness = 100,
    .energy = 100,
    .last_temp = 20.0,
    .last_hum = 50.0,
    .is_sleeping = false
};

// Mutex para proteger o acesso ao estado do Pet
SemaphoreHandle_t pet_state_mutex;

// Queue para eventos de botões
QueueHandle_t button_evt_queue;

// --- Tarefas (Tasks) FreeRTOS ---

// Task: Lê o sensor DHT20 periodicamente
void sensor_task(void *pvParameters) {
    i2c_master_dev_handle_t* sensorHandle = (i2c_master_dev_handle_t*)pvParameters;
    
    while(1) {
        float temperature = 0.0f;
        float humidity = 0.0f;
        
        // Ler dados do DHT20
        dht20_read_data_after_wait(*sensorHandle, &temperature, &humidity);
        
        // Atualizar o estado do Pet
        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            pet_state.last_temp = temperature;
            pet_state.last_hum = humidity;
            xSemaphoreGive(pet_state_mutex);
        }
        
        ESP_LOGI(TAG, "Sensor -> Temp: %.2fC | Hum: %.2f%%", temperature, humidity);
        vTaskDelay(pdMS_TO_TICKS(5000)); // Ler a cada 5 segundos
    }
}

// Task: Lógica Principal do Pet (Decaimento e Eventos)
void pet_logic_task(void *pvParameters) {
    uint32_t io_num;
    while(1) {
        // Processar eventos de botão da Queue (não bloqueante / timeout curto)
        if (xQueueReceive(button_evt_queue, &io_num, pdMS_TO_TICKS(1000))) {
            if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
                if (io_num == BUTTON_A_GPIO) {
                    pet_state.hunger = (pet_state.hunger + 20 > 100) ? 100 : pet_state.hunger + 20;
                    ESP_LOGI(TAG, "Botao A: Pet Alimentado!");
                } else if (io_num == BUTTON_B_GPIO) {
                    pet_state.happiness = (pet_state.happiness + 20 > 100) ? 100 : pet_state.happiness + 20;
                    pet_state.energy -= 10;
                    ESP_LOGI(TAG, "Botao B: Brincaste com o Pet!");
                } else if (io_num == BUTTON_C_GPIO) {
                    pet_state.is_sleeping = !pet_state.is_sleeping;
                    ESP_LOGI(TAG, "Botao C: Sleep toggled (%d)", pet_state.is_sleeping);
                }
                xSemaphoreGive(pet_state_mutex);
            }
        }
        
        // Decaimento natural a cada ciclo (ex: 1 segundo neste caso pelo timeout da queue)
        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            if (!pet_state.is_sleeping) {
                // Diminuir stats gradualmente
                // (Na realidade este decaimento deve ser mais lento, p.ex. usando um timer ou contador)
                pet_state.hunger = (pet_state.hunger > 0) ? pet_state.hunger - 1 : 0;
                
                // Se a temperatura estiver muito alta, a energia desce mais rápido
                if (pet_state.last_temp > 30.0) {
                    pet_state.energy = (pet_state.energy > 0) ? pet_state.energy - 2 : 0;
                } else {
                    pet_state.energy = (pet_state.energy > 0) ? pet_state.energy - 1 : 0;
                }
            } else {
                // A recuperar energia a dormir
                pet_state.energy = (pet_state.energy + 5 > 100) ? 100 : pet_state.energy + 5;
            }
            xSemaphoreGive(pet_state_mutex);
        }
    }
}

// Task: Atualizar Ecrã OLED (A implementar)
void display_task(void *pvParameters) {
    while(1) {
        if (xSemaphoreTake(pet_state_mutex, portMAX_DELAY)) {
            ESP_LOGI(TAG, "--- STATS --- Fome: %d | Feliz: %d | Energia: %d", 
                     pet_state.hunger, pet_state.happiness, pet_state.energy);
            xSemaphoreGive(pet_state_mutex);
        }
        
        // TODO: Renderizar com esp_lcd / lvgl no I2C OLED
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- Handler de Interrupções ---
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    // Envia o número do pino para a queue de eventos
    xQueueSendFromISR(button_evt_queue, &gpio_num, NULL);
}

// --- Setup e Main ---
void app_main(void) {
    ESP_LOGI(TAG, "A Iniciar o Virtual Pet...");

    // 1. Inicializar Mutex e Queues
    pet_state_mutex = xSemaphoreCreateMutex();
    button_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // 2. Configurar Botões e Interrupções
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Aciona quando prime o botão (pull-up -> low)
        .pin_bit_mask = (1ULL<<BUTTON_A_GPIO) | (1ULL<<BUTTON_B_GPIO) | (1ULL<<BUTTON_C_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_A_GPIO, gpio_isr_handler, (void*) BUTTON_A_GPIO);
    gpio_isr_handler_add(BUTTON_B_GPIO, gpio_isr_handler, (void*) BUTTON_B_GPIO);
    gpio_isr_handler_add(BUTTON_C_GPIO, gpio_isr_handler, (void*) BUTTON_C_GPIO);

    // 3. Inicializar barramento I2C e Sensores (DHT20)
    static i2c_master_bus_handle_t busHandle;
    static i2c_master_dev_handle_t dht20Handle;
    
    dht20_init(&busHandle, &dht20Handle, DHT20_ADDR, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);
    // TODO: Adicionar o OLED no mesmo busHandle

    // 4. Criar Tasks do FreeRTOS
    xTaskCreate(sensor_task, "sensor_task", 4096, (void*)&dht20Handle, 5, NULL);
    xTaskCreate(pet_logic_task, "pet_logic_task", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 4096, NULL, 4, NULL);
    
    // TODO: Criar a WiFi Task no futuro baseada no guiao8
    
    ESP_LOGI(TAG, "Sistema Inicializado com sucesso!");
    
    // O FreeRTOS toma o controlo a partir daqui
}
