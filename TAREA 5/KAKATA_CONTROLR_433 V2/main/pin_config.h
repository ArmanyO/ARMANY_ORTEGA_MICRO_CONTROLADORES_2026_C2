#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// ============================================================
// KAKATA RC433 V1 - ESP32-S3 Pin Configuration
// Derived from PCB layout (KAKATA_RC433_V1.kicad_pcb)
// Designer: Carlos Pichardo / ITLA-HUB (2026-02-22)
// ============================================================

// --- I2C Bus (shared: MPU-6050 + OLED) ---
#define PIN_I2C_SDA         6
#define PIN_I2C_SCL         7

// --- Joysticks (Analog X/Y + Digital Button) ---
#define PIN_JOY0_MT         4   // ADC1_CH3 - Joystick 0 top
#define PIN_JOY0_MD         5   // ADC1_CH4 - Joystick 0 middle
#define PIN_JOY1_MT         2   // ADC1_CH1 - Joystick 1 top
#define PIN_JOY1_MD         1   // ADC1_CH0 - Joystick 1 middle
#define PIN_JOY0_BTN        3   // Active-low, external 22k pull-up
#define PIN_JOY1_BTN        46  // Active-low, external 22k pull-up

// --- Arcade Buttons (active-low, 22k pull-up) ---
#define PIN_BTN_0           9
#define PIN_BTN_1           11
#define PIN_BTN_2           10
#define PIN_BTN_3           12

// --- Side Buttons (active-low, 22k pull-up) ---
#define PIN_BTNL1           42
#define PIN_BTNL2           41
#define PIN_BTNL3           40
#define PIN_BTNL4           39

// --- LEDs (driven through 74HC04 inverter, active-low) ---
#define PIN_LED1            45
#define PIN_LED2            48
#define PIN_LED3            47
#define PIN_LED4            21
#define PIN_LED5            14
#define PIN_LED6            13

// --- RF 433 MHz ---
#define PIN_RF_RX           18  // Receiver data output
#define PIN_RF_TX           17  // Transmitter data input
#define PIN_RF_ENABLE       15  // Transmitter enable (active-high)

// --- MPU-6050 ---
#define PIN_MPU_INT         16  // Interrupt output from MPU-6050

// --- Battery ---
#define PIN_VBAT            8   // ADC1_CH0 - Battery voltage divider

// --- USB (HW peripheral, not GPIO) ---
// USB_DN -> IO19 (USB_N)
// USB_DP -> IO20 (USB_P)

#endif // PIN_CONFIG_H