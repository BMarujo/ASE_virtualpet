# Report Writing Plan: Interactive IoT Virtual Pet

This document outlines a structured, section-by-section plan for writing a comprehensive 5-page academic report on Overleaf (LaTeX) for the "Interactive IoT Virtual Pet using ESP32-C6" project.

## General Guidelines for Overleaf
* **Format:** IEEE conference style or standard university academic format (two-column usually preferred, but single-column works depending on the template).
* **Length:** Approximately 5 pages of core content (excluding the cover page and references).
* **Tone:** Academic, technical, and objective. Avoid first-person pronouns (use "The system implements..." instead of "We implemented...").
* **Visuals:** Use high-quality vector graphics where possible. Ensure `circuit.png` is properly scaled and referenced.

---

## Page 1: Introduction and Hardware Architecture

### 1. Title and Abstract (approx. 0.5 pages)
* **Title:** A descriptive title (e.g., *Design and Implementation of an Interactive IoT Virtual Pet using ESP32-C6 and FreeRTOS*).
* **Abstract:** A 150-200 word summary of the project. Mention the core problem, the hardware used (ESP32-C6), the software paradigm (FreeRTOS), the communication protocols (I2C, SPI, ADC, MQTT), and the final results (a functioning, persistent, power-efficient IoT pet with a web dashboard).
* **Keywords:** ESP32, FreeRTOS, IoT, MQTT, SPI, Low Power.

### 2. Introduction (approx. 0.5 pages)
* Define the objective: Creating a modern, hardware-based "Tamagotchi" style virtual pet.
* Briefly list the requirements: Interactive stimuli, ambient environment reaction, autonomous decay logic, state persistence, power efficiency, and remote telemetry.
* Provide a brief overview of the document structure.

### 3. Hardware Architecture & Peripherals (approx. 1 page)
* **Overview:** Introduce the ESP32-C6 DevKitC-1 as the system's brain.
* **Include the Circuit Diagram:** Insert `circuit.png` here. Ensure you add a LaTeX figure caption (e.g., *Figure 1: Hardware schematic and GPIO routing*).
* **Peripheral Mapping:** Briefly list the peripherals and their roles:
    * **I2C Bus (DHT20):** SDA (GPIO 6), SCL (GPIO 7). Reads ambient temperature and humidity.
    * **SPI Bus (TFT ST7735 & SD Card):** Explain the shared bus architecture. MOSI (19), MISO (20), CLK (21). Explain how the **Chip Select (CS)** mechanism works to multiplex the bus (GPIO 22 for TFT, GPIO 18 for SD Card) ensuring no data collision.
    * **ADC (Potentiometer):** GPIO 1. Acts as an analog input to control the food bowl target.
    * **GPIO & Interrupts (Buttons & LED):** Buttons on GPIO 23, 0, and 4 (configured with pull-ups and negative edge interrupts). LED on GPIO 5.

---

## Page 2-3: Software Architecture and FreeRTOS Paradigm

### 4. Software Architecture & Concurrency
* **FreeRTOS Implementation:** Explain why a traditional sequential `while(1)` loop is insufficient. Introduce the multi-tasking approach using FreeRTOS.
* **Task Breakdown:** Describe the four concurrent threads:
    1. `sensor_task`: Handles I2C blocking calls without freezing the game.
    2. `pet_logic_task`: The main state machine and brain of the system.
    3. `display_task`: Handles UI rendering asynchronously.
    4. `wifi_telemetry_task`: Manages network latency and MQTT publishing independently.

### 5. Synchronization and Resource Management
* **Mutexes (`pet_state_mutex`):** Explain the Critical Section problem. Provide a small pseudo-code snippet showing how the Display Task and Logic Task safely read/write the shared `pet_state_t` structure.
* **Event Queues (`button_evt_queue`):** Detail the Interrupt Service Routines (ISRs). Explain how hardware interrupts (button presses) avoid blocking the CPU by instantly pushing the GPIO ID into a FreeRTOS Queue, deferring the heavy processing to the `pet_logic_task`.

### 6. Game Logic and Mechanics (Include Pseudo-code)
* Detail the state machine governing the pet (Health, Hunger, Energy, Happiness, Water).
* Mention the environmental influence (e.g., temperatures > 30ºC drain energy faster; relative positive deltas in humidity replenish water).
* **Pseudo-code Example (Logic Loop):**
  ```text
  Algorithm 1: Pet Logic Loop
  BEGIN
    WHILE True DO
      Wait for Button Queue Events (Non-blocking)
      IF Button A pressed THEN Play Animation & Adjust Stats
      IF Button B pressed THEN Save State to SD Card
      
      Read ADC Value
      Calculate Decay Multiplier
      
      Lock State Mutex
      Apply Time-based Decay (Hunger, Water, Energy)
      Recompute Global Health
      Unlock State Mutex
      
      IF Health < Critical Threshold THEN
         Trigger Light Sleep Mode
      END IF
      
      Delay(Tick_Rate)
    END WHILE
  END
  ```

---

## Page 4: Advanced Features (SPI Multiplexing, Sleep & IoT)

### 7. Peripheral Deep-Dive: SPI Multiplexing and FatFs
* Explain the technical challenge of using one SPI host (`SPI2_HOST`) for two different components (Screen and SD Card).
* Detail how the software initializes the bus globally, then attaches the SD Card using `sdspi_host`, and finally attaches the TFT using `st7735_add_device`.
* Explain the use of the `FatFs` file system to serialize the C-struct into a `VP2` tagged text file (`pet.txt`) for non-volatile persistence.

### 8. Power Management (Light Sleep)
* Explain the transition from Active Mode to Light Sleep.
* Contrast Light Sleep with Deep Sleep (RAM is retained, CPU is halted, peripherals are clock-gated).
* **Hardware specific fixes:** Detail the usage of `gpio_hold_en()` to freeze the backlight pin state (preventing screen flickering) and `gpio_wakeup_enable()` to exclusively allow GPIO 4 to wake the system.
* Mention the "Grace Period" algorithm implemented upon wake-up to allow user intervention before the system immediately sleeps again.

### 9. Connectivity (Wi-Fi & MQTT)
* Explain the Wi-Fi Station setup with persistent NVS storage.
* Detail the MQTT protocol architecture (Publish/Subscribe).
* Explain how the system handles TCP/IP race conditions before sleeping by utilizing FreeRTOS Direct Task Notifications (`xTaskNotifyGive`) to force the MQTT task to flush the final `"is_sleeping":true` packet before the CPU halts.

---

## Page 5: Web Dashboard and Conclusion

### 10. Web Dashboard & Two-Way Communication
* Briefly introduce the Node.js/Express backend acting as an MQTT-to-WebSocket bridge.
* Describe the Frontend (HTML/Tailwind) that visually represents the JSON payload from the ESP32.
* Highlight the two-way telemetry: The dashboard subscribes to `virtualpet/status` to animate the SVG pixel-art cat, but also publishes to `virtualpet/command`.
* Explain how the ESP32 intercepts the `play` command via an MQTT Event Handler and injects a mock hardware interrupt into the FreeRTOS Queue.

### 11. Conclusion
* Summarize the project achievements (successful integration of 5+ hardware peripherals, robust RTOS architecture, low-power states, and full IoT bidirectional control).
* Mention possible future work (e.g., adding a speaker for I2S audio, or a battery management IC).

### 12. References
* Cite the ESP-IDF Programming Guide.
* Cite the FreeRTOS Reference Manual.
* Cite the ST7735 and DHT20 datasheets.