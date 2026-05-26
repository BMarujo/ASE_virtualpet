# Projeto Final de ASE: Interactive IoT Virtual Pet (ESP32-C6)

Este diretório contém a implementação completa do projeto final de Arquitetura de Sistemas Embebidos (2025/26): **Interactive IoT Virtual Pet using ESP32**.

## 1. Visão Geral do Projeto
Trata-se de um sistema embutido interativo (semelhante a um *Tamagotchi* moderno) com estados dinâmicos que reage tanto a estímulos do utilizador (botões, potenciómetro) como a fatores ambientais (temperatura e humidade reais). Todo o ecossistema é suportado por uma arquitetura assíncrona baseada em **FreeRTOS**, com gestão inteligente de energia (Light Sleep), persistência de dados (SD Card) e conectividade IoT remota (Wi-Fi + MQTT).

---

## 2. Mapa de Hardware e Pinos (ESP32-C6)

A integração de hardware combina múltiplos protocolos de comunicação (I2C, SPI, ADC) a correr em simultâneo:

*   **Display TFT SPI (ST7735) & Cartão SD (Partilha de SPI):**
    *   *Nota: O barramento SPI (SPI2_HOST) é inicializado globalmente para suportar ambos os periféricos.*
    *   **MOSI (SI):** GPIO 19
    *   **MISO:** GPIO 20 (Necessário para a leitura do SD Card)
    *   **CLK (SCK):** GPIO 21
    *   **CS (Ecrã TFT):** GPIO 22
    *   **CS (SD Card):** GPIO 18
    *   **DC (Ecrã TFT):** GPIO 2
    *   **RST (Ecrã TFT):** GPIO 3
    *   **BL (Backlight):** GPIO 15 (Controlado dinamicamente no Sleep Mode através de `gpio_hold_en`)

*   **Sensores & Entradas:**
    *   **DHT20 (I2C):** SDA no GPIO 6, SCL no GPIO 7.
    *   **Potenciómetro (ADC1):** GPIO 1 (Mapeado para definir a "Intensidade" do jogo).

*   **Interação (Botões & LED):**
    *   **Botão A (Alimentar):** GPIO 23
    *   **Botão B (Guardar Estado no SD):** GPIO 0
    *   **Botão C (Acordar do Sleep):** GPIO 4 (Configurado com `esp_sleep_enable_gpio_wakeup`)
    *   **LED de Estado (Zzz):** GPIO 5 (Permanece aceso durante o Light Sleep)

---

## 3. Arquitetura de Software (FreeRTOS & Paralelismo)

O código foi desenhado para abandonar o paradigma super-loop sequencial (`while(1) { wait(); }`). Em vez disso, utiliza fortemente os recursos do **FreeRTOS** para paralelismo, concorrência e programação assíncrona:

### A. Tarefas Concorrentes (Threads / Tasks)
Foram instanciadas 4 tarefas principais (`xTaskCreate`) com prioridades específicas. Como o ESP32-C6 tem apenas 1 core (Unicore), o scheduler do FreeRTOS faz a troca de contexto preemptiva entre estas tarefas de forma invisível:
1.  **`sensor_task` (Prioridade 5):** Bloqueia-se num delay de 5 segundos. Acorda periodicamente, inicia a transação I2C para ler o ambiente via DHT20 e atualiza as variáveis partilhadas.
2.  **`pet_logic_task` (Prioridade 5):** O "Cérebro". Corre periodicamente para processar o decaimento natural das necessidades do animal (ex: aumento da fome). Faz também a verificação síncrona dos botões (através de Queues) e a leitura direta do ADC para calcular o multiplicador temporal.
3.  **`display_task` (Prioridade 4):** Exclusivamente dedicada a renderizar. Bloqueia-se para ler as variáveis de estado de forma segura e injeta as matrizes gráficas (sprites gerados em C) e o texto no ecrã TFT via SPI.
4.  **`wifi_telemetry_task` (Prioridade 4):** Trata de formatar toda a informação estrutural da máquina de estados num objeto `JSON` e publicá-lo para um Broker MQTT local a cada 10 segundos, de forma puramente assíncrona face à lógica do jogo.

### B. Sincronização e Assincronismo
*   **Filas (Queues):** O tratamento dos cliques nos botões nunca é feito de forma bloqueante. Existe uma *Interrupt Service Routine (ISR)* agregada aos pinos dos botões (`gpio_isr_handler`). Quando pressionas um botão, a execução é interrompida por microssegundos apenas para colocar o ID do pino numa Fila (`button_evt_queue`). A `pet_logic_task` consome esta fila assim que puder, lidando com a ação de forma fluída e *debounce-safe*.
*   **Mutexes:** Variáveis partilhadas (como `pet_state.hunger` ou `pet_state.last_temp`) que são lidas pela *display task* e pela *telemetry task*, mas escritas pela *sensor task* e *logic task*, estão protegidas por um Semáforo de Exclusão Mútua (`pet_state_mutex`). Isto previne *race conditions* (ex: o ecrã tentar ler a fome exatamente no milissegundo em que a rotina de decaimento a está a alterar).

---

## 4. Mecânicas e Funcionalidades Especiais

1.  **O ADC como Modificador Físico:** A leitura no Pino 1 altera diretamente a "Intensidade" do jogo. Rodar o potenciómetro modifica o `decay_multiplier` de 1 a 6. Num nível alto, o pet perde a fome assustadoramente rápido e a energia gasta a brincar dispara, tornando o jogo mais difícil.
2.  **Influência Ambiental:** Se a temperatura ambiente real lida pelo sensor I2C (DHT20) for superior a 30ºC, o animal sofre de desgaste térmico e a sua barrinha de Energia cai ao dobro da velocidade (mesmo que estejas a ser brando no potenciómetro).
3.  **Light Sleep & Low Power Mode:** 
    *   Quando a "Fome" baixa do patamar crítico (< 20), o ESP32 desliga o ecrã, acende o LED do Pino 5 (para saberes que não está morto, apenas a dormir) e invoca a suspensão do CPU (`esp_light_sleep_start()`).
    *   Ao contrário do Deep Sleep, o estado do jogo é mantido na RAM.
    *   Pressionar o **Botão C (GPIO 4)** acorda o microcontrolador a meio da execução. O sistema dá um *Grace Period* de 10 segundos ao utilizador para lhe dar de comer e tirar o boneco da zona de perigo antes de voltar a fechar o ecrã.
4.  **Persistência em SD Card (FAT):** Apesar de a RAM não se apagar no Light Sleep, um *reset* por falta de bateria ou remoção de cabo faria o Pet voltar sempre à Fome 100%. Para evitar isso, ao arrancar, o sistema monta e lê o sistema de ficheiros do cartão SD. Tens à tua disposição o **Botão B (GPIO 0)**, que age como uma ação de "Save Game" forçada, escrevendo as métricas no cartão num instante para posterior recuperação de contexto.

---

## 5. Como Compilar e Executar

A *Partition Table* foi estendida para 4MB Flash (aumentando a `factory app` capacity) devido à vastidão de bibliotecas associadas (SDMMC, FatFs, WiFi, MQTT, Ecrã TFT). O SDK também já grava o teu Wi-Fi nativo para sobreviver a operações de limpeza (`fullclean`).

```bash
# Entrar no ambiente ESP-IDF
source ~/esp/esp-idf/export.sh
cd /path/to/project/virtual-pet

# Compilar e Flashar para a placa (USB detetado automaticamente)
idf.py build
idf.py flash

# Se quiseres ver os logs assíncronos a funcionar:
idf.py monitor
```

Para intercetares a telemetria que está a ser publicada para a Cloud Local:
```bash
mosquitto_sub -h 10.229.103.1 -p 1883 -t "virtualpet/status" -v
```