# IoT Climate Control - Embedded Firmware

C++ firmware for the **Arduino Uno R4 WiFi** node in the IoT Climate Control ecosystem. It reads environmental telemetry from a Bosch BME280 sensor, updates a local 16x2 LCD, communicates with an cloud MQTT broker over MQTTS (TLS 8883), and controls an air conditioner via IR bit-banging modulation.

## 🔗 Companion Repository

- 🌐 **Fullstack Web Platform:** [iot_control_fullstack](https://github.com/adrianp59/iot_control_fullstack) — Django REST API, React Dashboard, Mosquitto MQTT broker, and Traefik SSL deployment.

## Features

- **Non-blocking Super-loop Architecture:** Asynchronous task scheduler using `millis()` without blocking `delay()` calls.
- **Bit-Banging IR Driver:** Custom 38 kHz carrier frequency pulse generator (`pulse38kHz`) isolated from hardware timers to prevent interrupt conflicts with the ESP32 Wi-Fi coprocessor.
- **Secure MQTTS Transport:** Connects to port 8883 using `WiFiSSLClient` with TLS encryption and user credentials.
- **Hardware Watchdog Protection:** Integrated Renesas RA4M1 Watchdog Timer (4000 ms window) for hard reset automatic recovery upon I2C or network freeze.
- **Fault-Tolerant Reconnections:** Asynchronous `handleNetworkReconnections()` state machine.
- **Local HMI Display:** 16x2 LiquidCrystal display for real-time telemetry and network connection status.

## Hardware Stack & Circuitry

- **Microcontroller:** Arduino Uno R4 WiFi (Arm Cortex-M4 Renesas RA4M1 @ 48MHz + ESP32-S3)
- **Sensor:** Bosch BME280 (I2C at `0x76` or `0x77`)
- **IR Driver (Low-Side Switch):**
  - Transistor: 2N2222 NPN (TO-18)
  - Base resistor: $750\,\Omega$ (limits GPIO current $< 5\text{ mA}$)
  - IR Emitter: Vishay TSAL6400 ($940\text{ nm}$, $\pm 25^\circ$)
  - Collector resistor: $47\,\Omega$ ($I_F \approx 71.3\text{ mA}$)
  - Power Buffer: $470\,\mu\text{F}$ electrolytic capacitor on the 5V bus
- **Local HMI:** 16x2 Parallel LCD

## Project Structure

```text
.
├── include/             # Header files
├── lib/                 # Custom local libraries
├── src/
│   ├── main.cpp         # Super-loop logic, MQTT callbacks & task manager
│   ├── ac_commands.h    # Raw 88-bit Mitsubishi IR command bit arrays
│   └── config.h.example # Configuration template (Wi-Fi, MQTT credentials)
├── platformio.ini       # PlatformIO configuration & dependencies
└── .gitignore
```

## Setup & Flashing

1. **Open in VS Code with PlatformIO Extension installed.**
2. **Configure credentials:**
   Rename `src/config.h.example` to `src/config.h` and set your credentials:
   ```cpp
   #define WIFI_SSID     "Your_WiFi_Name"
   #define WIFI_PASS     "Your_WiFi_Password"
   #define MQTT_SERVER   "iot.yourdomain.com"
   #define MQTT_PORT     8883
   #define MQTT_USER     "device_user"
   #define MQTT_PASS     "device_password"
   ```
3. **Build and Upload:**
   Connect your Arduino Uno R4 WiFi via USB and run PlatformIO Build & Upload (`Ctrl+Alt+U` or via PIO toolbar).

## License

This project is licensed under the [MIT License](https://www.google.com/search?q=LICENSE).
