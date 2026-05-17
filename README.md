# Projeto Final de ASE: Interactive IoT Virtual Pet (ESP32-C6)

Este diretório contém a estrutura base e o plano de desenvolvimento para o projeto final de Arquitetura de Sistemas Embebidos: **Interactive IoT Virtual Pet using ESP32**.

## 1. Visão Geral do Projeto
O objetivo é desenvolver um "Tamagotchi" moderno, interativo e ligado à rede. O animal virtual terá necessidades (fome, felicidade, energia) que decaem com o tempo e que variam de acordo com o ambiente (temperatura e humidade do sensor DHT20). O estado será apresentado no Ecrã OLED (via I2C) e a interação será feita através de botões físicos. Adicionalmente, o estado será reportado por WiFi para monitorização remota e o dispositivo usará Sleep Modes para poupar energia quando o pet estiver a "dormir".

## 2. Componentes e Configuração de Pinos (ESP32-C6)

Abaixo está o mapa completo de pinos a utilizar no projeto, recolhido dos diversos guiões (incluindo o guião 10):

*   **Display SPI (TFT ST7735):**
    *   **MOSI (SI):** GPIO 19
    *   **CLK (SCK):** GPIO 21
    *   **CS (TCS):** GPIO 22
    *   **DC:** GPIO 2
    *   **RST:** GPIO 3
    *   **BL (Backlight):** GPIO 15
*   **Sensor I2C (DHT20):**
    *   **SDA:** GPIO 6
    *   **SCL:** GPIO 7
*   **Botões de Interação:**
    *   **Botão A (Alimentar/Confirmar):** GPIO 23 (Aproveitando o pino do guião 3)
    *   **Botão B (Brincar/Mudar Menu):** GPIO 9
    *   **Botão C (Dormir/Acordar):** GPIO 4 (Configurado para Ext1 Wake-up do Deep Sleep).
*   **Outros Componentes (Opcionais/Estado):**
    *   **LED de Estado:** GPIO 5
    *   **Leitura ADC:** GPIO 11
*   **Comunicação:** WiFi (Antena Interna)

## 3. Arquitetura e FreeRTOS

O sistema será gerido pelo **FreeRTOS** com as seguintes Tarefas (Tasks):

1.  **Pet_Logic_Task:** Task principal que atualiza os estados internos do pet (fome, sono, felicidade) a cada X segundos com base numa máquina de estados e temporizadores.
2.  **Sensor_Task:** Task que lê o DHT20 (temperatura/humidade) e ajusta as taxas de decaimento (ex: o pet fica mais cansado se estiver muito calor).
3.  **Display_Task:** Lê o estado atual (via filas/queues ou variáveis globais protegidas por Mutex) e renderiza a cara e o estado no ecrã TFT SPI (ST7735).
4.  **WiFi_Telemetry_Task:** Envia o estado do pet para um servidor (HTTP/MQTT) a cada minuto.
5.  **Button_ISR_Handler (Interrupções):** Interrupções nos pinos GPIO dos botões vão inserir eventos numa Queue para serem processados pela Pet_Logic_Task.

## 4. Plano de Desenvolvimento Passo-a-Passo

### Passo 1: Configuração Base e Sensores (I2C)
*   [x] Estrutura do projeto criada (`CMakeLists.txt`, pasta `main`).
*   [x] Importação do driver do DHT20 do `guiao5`.
*   [ ] Implementar inicialização do bus I2C no `main.c`.
*   [ ] Configurar a leitura periódica do DHT20 usando uma task FreeRTOS.
*   **Teste:** Verificar via UART (ESP_LOGI) se a temperatura e humidade são lidas corretamente.

### Passo 2: Integração do Ecrã TFT (ST7735 via SPI)
*   [x] Copiar a componente `st7735_driver` do guião 10 para o projeto.
*   [ ] Inicializar o painel TFT usando o barramento SPI e os pinos configurados.
*   [ ] Desenhar a interface base: Cara do pet, texto e barras de progresso (Fome, Energia).
*   **Teste:** O ecrã deverá mostrar imagens estáticas e dados da temperatura.

### Passo 3: Lógica do "Virtual Pet" e FreeRTOS
*   [ ] Criar uma `struct` para guardar o estado do pet (hp, fome, energia, humor).
*   [ ] Criar a `Pet_Logic_Task` que decrementa periodicamente estes atributos. Se a fome chegar a 0, perde HP.
*   [ ] Adicionar Mutexes (`xSemaphoreCreateMutex`) para proteger a leitura/escrita do estado do pet partilhado entre as tasks.

### Passo 4: Interação com Botões
*   [ ] Configurar os GPIOs dos botões com resistências de Pull-up (se não forem externos).
*   [ ] Associar interrupções (`gpio_install_isr_service`) para colocar eventos de botão numa Queue (`xQueueCreate`).
*   [ ] Atualizar a `Pet_Logic_Task` para ler a Queue e atuar em conformidade (Alimentar repõe fome, Brincar aumenta felicidade mas reduz energia).
*   **Teste:** Clicar num botão deve atualizar a barra respetiva no ecrã OLED.

### Passo 5: Sleep Modes e Gestão de Energia (Low Power)
*   [ ] Implementar o estado "Dormir" para o Pet. Quando acionado, o ESP32 deve desligar o ecrã OLED e o WiFi.
*   [ ] Chamar `esp_deep_sleep_start()` configurando o botão C (ex: GPIO 4) como `esp_sleep_enable_ext1_wakeup()`.
*   [ ] Ao acordar, recuperar o estado a partir da RTC Memory ou NVS para o pet continuar como estava.
*   **Teste:** O sistema desliga-se. Pressionar o botão acorda o dispositivo de imediato restaurando as barras no ecrã.

### Passo 6: Conectividade WiFi (Opcional/Remoto)
*   [ ] Incorporar a configuração Station WiFi do `guiao8`.
*   [ ] Criar uma task que emita dados HTTP POST simples, num formato JSON, para um Webhook de testes (ex: webhook.site).
*   **Teste:** Validar a receção dos JSONs pela cloud confirmando o estado ao vivo do pet.

## 5. Como Iniciar o Desenvolvimento

Os ficheiros base estão já configurados nesta diretoria. Para compilar o que tens até agora, corre no terminal:

```bash
idf.py set-target esp32c6
idf.py menuconfig
idf.py build
idf.py -p PORT flash monitor
```
*(No menuconfig podes ajustar definições do FreeRTOS, WiFi ou componentes LCD que vires a necessitar).*
