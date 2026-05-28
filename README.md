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
2.  **`pet_logic_task` (Prioridade 5):** O "Cérebro". Corre periodicamente para processar a fome, energia, felicidade, tigela de comida, hidratação e saúde. Também consome os eventos dos botões e lê o ADC para animar/encher a tigela.
3.  **`display_task` (Prioridade 4):** Exclusivamente dedicada a renderizar. Bloqueia-se para ler as variáveis de estado de forma segura e injeta as matrizes gráficas (sprites gerados em C) e o texto no ecrã TFT via SPI.
4.  **`wifi_telemetry_task` (Prioridade 4):** Trata de formatar toda a informação estrutural da máquina de estados num objeto `JSON` e publicá-lo para um Broker MQTT local a cada 10 segundos, de forma puramente assíncrona face à lógica do jogo.

### B. Sincronização e Assincronismo
*   **Filas (Queues):** O tratamento dos cliques nos botões nunca é feito de forma bloqueante. Existe uma *Interrupt Service Routine (ISR)* agregada aos pinos dos botões (`gpio_isr_handler`). Quando pressionas um botão, a execução é interrompida por microssegundos apenas para colocar o ID do pino numa Fila (`button_evt_queue`). A `pet_logic_task` consome esta fila assim que puder, lidando com a ação de forma fluída e *debounce-safe*.
*   **Mutexes:** Variáveis partilhadas (como `pet_state.hunger` ou `pet_state.last_temp`) que são lidas pela *display task* e pela *telemetry task*, mas escritas pela *sensor task* e *logic task*, estão protegidas por um Semáforo de Exclusão Mútua (`pet_state_mutex`). Isto previne *race conditions* (ex: o ecrã tentar ler a fome exatamente no milissegundo em que a rotina de decaimento a está a alterar).

---

## 4. Mecânicas e Funcionalidades Especiais

1.  **Potenciómetro como Tigela de Comida:** A leitura no GPIO 1 deixou de alterar a velocidade da fome. Agora define o nível-alvo da tigela de comida (`food_bowl_target`), com animação por frames de pixel art em `main/assets/`. O gato come da tigela aos poucos, recuperando fome e gastando a comida visível.
2.  **Botão A como Brincadeira:** O antigo botão de alimentar agora faz o gato executar um flip animado. A ação aumenta felicidade, custa um pouco de energia e não repõe a fome diretamente.
3.  **Humidade como Medidor de Água:** O DHT20 cria uma linha de base da humidade. A subida positiva (`humidity_delta`) enche o medidor H2O proporcionalmente: +5 pontos percentuais dão cerca de 25%, +20 pontos percentuais chegam a 100%.
4.  **Barra de Saúde:** A saúde é calculada por uma média ponderada de fome, felicidade, energia, H2O e comida disponível, com penalização por temperatura acima de 30ºC. Fórmula atual: fome 30%, felicidade 20%, energia 20%, H2O 20%, tigela 10%, menos penalização térmica.
5.  **Light Sleep & Low Power Mode:** 
    *   O Light Sleep passa a ser ativado quando a Saúde desce abaixo de 25%.
    *   O ESP32 desliga o backlight, acende o LED do GPIO 5 e invoca `esp_light_sleep_start()`.
    *   Ao contrário do Deep Sleep, o estado do jogo é mantido na RAM.
    *   Pressionar o **Botão C (GPIO 4)** acorda o microcontrolador. O sistema dá um *Grace Period* de 10 segundos antes de voltar a dormir caso a saúde continue crítica.
6.  **Persistência em SD Card (FAT):** O **Botão B (GPIO 0)** guarda o estado completo no SD: fome, felicidade, energia, tigela, alvo da tigela, H2O, saúde, temperatura, humidade, linha de base e delta de humidade. O boot continua a suportar o ficheiro antigo que guardava apenas a fome, migrando-o automaticamente.
7.  **Telemetria MQTT:** O tópico `virtualpet/status` inclui agora `food_bowl`, `food_bowl_target`, `water_meter`, `health`, `humidity_delta`, `is_sleeping`, `is_flipping` e `expression`, além dos valores já existentes.

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
