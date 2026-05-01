# 🔧 Hardware List

A complete bill of materials for the ESP32-based 3-phase AC energy monitor with relay protection.

---

## 1. Core Controller

| Component | Qty | Notes |
|-----------|-----|-------|
| ESP32 Development Board (NodeMCU-32S) | 1x | Using 12-bit ADC resolution (0–4095) |

---

## 2. Energy Sensors

| Component | Qty | Pin(s) | Notes |
|-----------|-----|--------|-------|
| ZMPT101B Voltage Sensor Module | 1x | GPIO 34 | Provides AC sine wave reference |
| ZMCT103C Current Sensor Module | 3x | GPIO 35, 32, 33 | High precision for low-power loads (e.g., phone chargers); rated ~5A / ~1100W @ 220V — fits within the 1000W global overload limit |

---

## 3. Actuators & Control

| Component | Qty | Pin(s) | Notes |
|-----------|-----|--------|-------|
| 4-Channel Relay Module (5V, Active-Low) | 1x | GPIO 27, 5, 13 | Physical safety cutoff for three loads |
| Logic Level Shifter (Bi-directional) | 1x | — | *Optional but recommended* — ensures reliable relay triggering if the module requires 5V logic and ESP32 outputs 3.3V |

---

## 4. Power & Protection

| Component | Qty | Notes |
|-----------|-----|-------|
| 5V 2A DC Power Adapter | 1x | Powers the ESP32 and all 3 relay coils simultaneously |
| 2-Pin Screw Terminal | 3x | AC line pass-through connections for ZMCT103C sensors |
| Project Enclosure / Box | 1x | Required to safely house exposed 220V wiring from ZMCT103C pass-through holes |

---

## ⚠️ Safety Notes

- The ZMCT103C sensors require the AC live wire to be physically passed through (or soldered through) a small hole on the sensor PCB. Always ensure these connections are **fully enclosed** inside a secure project box.
- All 220V wiring must be handled with appropriate insulation and strain relief.
- The relay module switches mains voltage — verify your relay ratings match your load requirements before powering on.

---

## Pin Summary

| Signal | GPIO |
|--------|------|
| Voltage Sensor (ZMPT101B) | 34 |
| Current Sensor — Load 1 (ZMCT103C) | 35 |
| Current Sensor — Load 2 (ZMCT103C) | 32 |
| Current Sensor — Load 3 (ZMCT103C) | 33 |
| Relay — Load 1 | 27 |
| Relay — Load 2 | 5 |
| Relay — Load 3 | 13 |
