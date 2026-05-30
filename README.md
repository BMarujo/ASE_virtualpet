# Projeto Final de ASE: Interactive IoT Virtual Pet (ESP32-C6)

Este diretório contém a implementação completa e refatorada do projeto final de Arquitetura de Sistemas Embebidos (2025/26): **Interactive IoT Virtual Pet using ESP32**.

## 1. Visão Geral do Projeto
Trata-se de um sistema embutido avançado (um *Tamagotchi* IoT) com lógicas e mecânicas de jogo complexas. O sistema reage a estímulos analógicos (potenciómetro), digitais (botões) e ambientais (sensor de temperatura e humidade). 

Todo o ecossistema é suportado por uma arquitetura assíncrona baseada no RTOS da Espressif (**FreeRTOS**), desenhado com boas práticas de concorrência, otimização de renderização gráfica, gestão de energia (Light Sleep), persistência de dados em cartão (SD Card FATFs) e conectividade à Cloud (Wi-Fi + MQTT).

---

## 2. Mapa de Hardware e Pinos (ESP32-C6)

O hardware integra múltiplos protocolos de comunicação (I2C, SPI, ADC) a funcionar em simultâneo no ESP32-C6:

*   **Display TFT SPI (ST7735) & Cartão SD (Partilha de SPI):**
    *   *Nota: O barramento SPI (SPI2_HOST) é partilhado entre o TFT e o Cartão SD para poupar pinos.*
    *   **MOSI (SI):** GPIO 19
    *   **MISO:** GPIO 20 (Para leitura do SD Card)
    *   **CLK (SCK):** GPIO 21
    *   **CS (Ecrã TFT):** GPIO 22
    *   **CS (SD Card):** GPIO 18
    *   **DC (Ecrã TFT):** GPIO 2
    *   **RST (Ecrã TFT):** GPIO 3
    *   **BL (Backlight):** GPIO 15 (Controlado dinamicamente para desligar a luz no Sleep Mode)

*   **Sensores & Entradas:**
    *   **DHT20 (I2C):** SDA no GPIO 6, SCL no GPIO 7. (Lê Temperatura e Humidade).
    *   **Potenciómetro (ADC1):** GPIO 1 (Controla o nível da "Tigela de Comida").

*   **Interação (Botões & LED):**
    *   **Botão A (Ação/Brincar):** GPIO 23 (Faz o gato dar um "Flip" / mortal).
    *   **Botão B (Guardar no SD):** GPIO 0 (Guarda o estado do Pet no Cartão SD).
    *   **Botão C (Acordar):** GPIO 4 (Serve estritamente para acordar a placa do modo Light Sleep).
    *   **LED de Estado:** GPIO 5 (Fica aceso permanentemente durante o modo Light Sleep).

---

## 3. Arquitetura de Software (FreeRTOS)

O projeto tira partido do paralelismo e assincronismo do FreeRTOS através de quatro *Tasks* que correm de forma concorrente no único núcleo do ESP32-C6:

1.  **`sensor_task` (I2C):** Bloqueia-se num delay de 5 segundos. Quando acorda, comunica com o DHT20. Se a humidade subir acima de um *baseline* local (ex: o utilizador sopra para o sensor), o nível de "Água" do gato sobe.
2.  **`pet_logic_task` (O Cérebro):** Executa a lógica do jogo (fome, energia, comer automático). Lê a Fila (*Queue*) de eventos dos botões e reage. Executa as contas da "Saúde" global.
3.  **`display_task` (Renderização):** Task otimizada. Só atualiza as áreas de ecrã (barras, texto) que efetivamente sofreram alterações face ao *frame* anterior. Permite taxas de atualização muito rápidas (ex: 150ms durante a animação do *Flip*).
4.  **`wifi_telemetry_task` (IoT):** A cada 10 segundos formata um JSON com todo o estado e publica via MQTT. Sendo uma Task independente, falhas na rede ou latências de internet não bloqueiam o jogo nem o ecrã.

### Sincronização
*   **Filas (Queues):** Os botões disparam rotinas de interrupção em hardware (ISRs). A ISR insere rapidamente o pino numa Queue (`button_evt_queue`), delegando o processamento pesado à `pet_logic_task`.
*   **Mutexes:** A estrutura `pet_state_t` é lida por quase todas as tasks. Para evitar corrupção de memória e *race conditions*, cada task tem de adquirir o `pet_state_mutex` antes de ler ou escrever nos status.

---

## 4. Lógica de Jogo e Mecânicas

### A. Estatísticas e Saúde (HP)
O estado do Pet é governado por uma variável mestre: a **Saúde (Health)**. Esta é calculada como uma média pesada da: Fome (30%), Felicidade (20%), Energia (20%), Água (20%) e quantidade de Comida na Tigela (10%). Adicionalmente, se a temperatura (Sensor I2C) for superior a 30ºC, a saúde sofre uma **penalização térmica** pesada.

### B. Tigela de Comida Dinâmica (Potenciómetro ADC)
O potenciômetro não alimenta o gato magicamente. Em vez disso, ele controla uma **Tigela de Comida**. Rodar o potenciómetro define o `target` da tigela. Lentamente, a comida cai para a tigela até atingir esse alvo.
A cada dois segundos, se a tigela tiver comida, o gato vai **comer autonomamente**, reduzindo o conteúdo da tigela, satisfazendo a fome e aumentando ligeiramente a felicidade.

### C. Água e Respiração (Sensor de Humidade DHT20)
O sistema calibra um `baseline` de humidade no arranque. Se o utilizador respirar/soprar diretamente para o sensor DHT20, o delta de humidade aumenta exponencialmente, o que se traduz em recarregar o "Medidor de Água" (`H2O`) da personagem!

### D. Animações e Cansaço (Botão A)
Clicar no Botão A (GPIO 23) faz o gato executar um "Flip" (animação por *frames* renderizada no ecrã). Isto diverte o gato (ganha felicidade), mas tem um custo claro de energia e fome.

### E. Persistência de Estado (Botão B e SD Card)
Clicar no Botão B (GPIO 0) força um *Save* completo. A estrutura inteira do gato (Fome, Energia, Água, Felicidade, etc.) é gravada num ficheiro (`pet.txt`) no Cartão SD no formato de tags `VP2`. Se a placa for desligada, quando voltar a ser ligada, a `app_main` monta o cartão SD, descobre o save, e carrega imediatamente os dados de volta para a RAM.

### F. Modo Light Sleep e Grace Period
Se a **Saúde Global (HP)** cair abaixo do threshold crítico (< 25):
*   O sistema entra em **Light Sleep** (`esp_light_sleep_start()`), pausando o CPU e a lógica do jogo mas mantendo a RAM intocável.
*   O ecrã apaga a sua luz de fundo (para poupar bateria), e o LED (GPIO 5) acende indicando o coma/sono.
*   O único modo de acordar o ESP32 é clicando no **Botão C (GPIO 4)**. 
*   Ao acordar, o jogo não volta a dormir de imediato! Há um **Período de Graça de 10 Segundos**. Durante estes 10 segundos o utilizador tem de rodar o potenciómetro para encher a tigela, ou soprar para a água, de modo a subir a Saúde do gato para cima de 25, ou ele voltará a desmaiar.

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

# Se quiseres ver os logs da lógica de jogo:
idf.py monitor
```

Para intercetares a telemetria que está a ser publicada para a Cloud Local:
```bash
mosquitto_sub -h 10.229.103.1 -p 1883 -t "virtualpet/status" -v
```