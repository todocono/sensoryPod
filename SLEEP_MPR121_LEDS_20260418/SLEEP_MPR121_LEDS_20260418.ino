// Using https://forum.seeedstudio.com/t/sleep-current-of-xiao-nrf52840-deep-sleep-vs-light-sleep/271841/36
// and https://forum.seeedstudio.com/t/getting-lower-power-consumption-on-seeed-xiao-nrf52840/270129/76
// For charging current and reading Vbat, I also used:
// https://forum.seeedstudio.com/t/xiao-ble-sense-battery-level-and-charging-status/263248




// // #include "nrf_gpio.h"
// // #include "nrf_power.h"



#include <SlowSoftI2CMaster.h>
#include "Adafruit_TinyUSB.h"
#include <FastLED.h>

// --- LED CONFIGURATION ---
#define NUM_LEDS 28
#define DATA_PIN D10
#define CLOCK_PIN 13 
CRGB leds[NUM_LEDS];

// --- EXACT LED MAPPINGS ---
const int ele0_leds[] = { 23, 24, 25, 26 };
const int ele1_leds[] = { 27, 16, 17, 18 };
const int ele2_leds[] = { 13, 14, 15 };
const int ele3_leds[] = { 1, 2 };
const int ele4_leds[] = { 5, 6 };
const int ele5_leds[] = { 10, 11, 12 };
const int ele6_leds[] = { 3, 4 };
const int ele7_leds[] = { 0 };
const int ele8_leds[] = { 7, 8, 9 };
const int ele9_leds[] = { 19, 20, 21, 22 };

// Master array to easily loop through them
const int* led_maps[10] = { ele0_leds, ele1_leds, ele2_leds, ele3_leds, ele4_leds, ele5_leds, ele6_leds, ele7_leds, ele8_leds, ele9_leds };
const int led_sizes[10] = { 4, 4, 3, 2, 2, 3, 2, 1, 3, 4 };

// Array to track the smooth fading state for each electrode
float currentBrightness[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// --- BATTERY & POWER (XIAO nRF52840) ---
#define HIGH_CHARGE_PIN 22  

#ifndef VBAT_ENABLE
  #define VBAT_ENABLE 14
#endif
#ifndef PIN_VBAT
  #define PIN_VBAT 32
#endif

#define BAT_MAX_RAW 1760 
#define BAT_MIN_RAW 1340

// --- HARDWARE INTERRUPT (For Waking Up) ---
#define IRQ_PIN D2
volatile bool mpr121_irq_triggered = false;
void wakeISR() {
  mpr121_irq_triggered = true;
}

// --- SLEEP & BEHAVIOR SETTINGS ---
#define SLEEP_TIMEOUT 10000  // 10 seconds of inactivity to sleep
#define WAKE_HOLD_TIME 1000  // Hold ELE7 for 1 second to wake
unsigned long lastActivityTime = 0;
unsigned long lastBatteryCheck = 0;
bool isAsleep = false;
uint8_t globalHue = 96;  // Start assuming Green
uint8_t globalSat = 0;   // Start assuming White

// --- MPR121 CONFIGURATION ---
#define MPR121_I2C_ADDRESS 0x5A  
#define MPR121_READ_LENGTH 0x2B  
#define TouchThre 6  
#define ReleaThre 4  

uint8_t readingArray[MPR121_READ_LENGTH];
int16_t ele_delta[13];  

struct TouchStatus {
  uint8_t Reg0;
  uint8_t Reg1;
  bool Touched;  
};

TouchStatus CurrTouchStatus;
TouchStatus PreTouchStatus = { 0, 0, false };  

SlowSoftI2CMaster si = SlowSoftI2CMaster(D5, D4, true);

void setup() {
  Serial.begin(115200);
  
  // Safe Boot Wait
  while (!Serial && millis() < 3000);
  Serial.println("\nMPR Test");

  // --- 1. POWER & BATTERY INIT ---
  pinMode(HIGH_CHARGE_PIN, OUTPUT);
  digitalWrite(HIGH_CHARGE_PIN, LOW);  // 100mA charge
  
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH);     // Turn off divider to save power
  
  analogReadResolution(12);

  // --- 2. MPR121 INIT ---
  if (!si.i2c_init())
    Serial.println(F("Initialization error. SDA or SCL are low"));
  else
    Serial.println(F("I2C Init...done"));
    
  MPR121_init_arduino();
  PreTouchStatus.Touched = false;  

  // --- 3. INTERRUPT INIT ---
  pinMode(IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(IRQ_PIN), wakeISR, FALLING);

  // --- 4. FASTLED INIT ---
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 400); 

  for (int ledN = 0; ledN < NUM_LEDS; ledN++) {
    leds[ledN] = CRGB::Red;
    FastLED.show();
    delay(50); 
  }
  for (int ledN = NUM_LEDS - 1; ledN >= 0; ledN--) {
    leds[ledN] = CRGB::Black;
    FastLED.show();
    delay(10);
  }

  lastActivityTime = millis();
}

void loop() {
  // ==========================================
  // 1. DEEP SLEEP & WAKE LOGIC
  // ==========================================
  if (isAsleep) {
    __SEV();
    __WFE();
    __WFE();

    // Board woke up via IRQ. Wait for ELE7 hold.
    mpr121_irq_triggered = false;
    Read_MPR121_ele_register_arduino();
    Get_touch_status_arduino();
    uint16_t touchData = CurrTouchStatus.Reg0 | ((uint16_t)(CurrTouchStatus.Reg1 & 0x0F) << 8);

    if (touchData & (1 << 7)) { // Checking ELE7
      unsigned long holdStart = millis();
      bool held = true;

      while (millis() - holdStart < WAKE_HOLD_TIME) {
        Read_MPR121_ele_register_arduino();
        Get_touch_status_arduino();
        uint16_t checkData = CurrTouchStatus.Reg0 | ((uint16_t)(CurrTouchStatus.Reg1 & 0x0F) << 8);

        if (!(checkData & (1 << 7))) {
          held = false;
          break;
        }
        delay(10);
      }

      if (held) {
        isAsleep = false;
        lastActivityTime = millis();
        lastBatteryCheck = 0; 
        
        // --- Calculate and print Battery Voltage on Wake ---
        digitalWrite(VBAT_ENABLE, LOW); 
        delay(10);                       
        int rawBat = analogRead(PIN_VBAT);
        digitalWrite(VBAT_ENABLE, HIGH); 
        
        // nRF52840 divider math: (raw / 4095) * 3.3V * ~2.96 divider ratio
        float voltage = (rawBat / 4095.0) * 3.3 * (1510.0 / 510.0);
        
        Serial.println("============================");
        Serial.print("WOKE UP! Battery: ");
        Serial.print(voltage);
        Serial.println(" V");
        Serial.println("============================");
      }
    }
    return; // Stay trapped here if it wasn't a valid wake sequence
  }

  // ==========================================
  // 2. SENSOR READING
  // ==========================================
  Read_MPR121_ele_register_arduino();  
  Get_ele_data_arduino();              
  Get_touch_status_arduino();          

  // ==========================================
  // 3. INACTIVITY TIMEOUT CHECK
  // ==========================================
  if (CurrTouchStatus.Touched) {
    lastActivityTime = millis(); 
  } else if (millis() - lastActivityTime > SLEEP_TIMEOUT) {
    for (int i = 0; i < 50; i++) {
      fadeToBlackBy(leds, NUM_LEDS, 20);
      FastLED.show();
      delay(20);
    }
    FastLED.clear();
    FastLED.show();
    
    // Reset our smoothing array so it starts from zero next time we wake up
    for(int i = 0; i < 10; i++) currentBrightness[i] = 0;
    
    isAsleep = true;
    Serial.println("Going to sleep...");
    return;
  }

  // ==========================================
  // 4. BATTERY & COLOR CALCULATION
  // ==========================================
  if (millis() - lastBatteryCheck > 5000 || lastBatteryCheck == 0) {
    lastBatteryCheck = millis();

    digitalWrite(VBAT_ENABLE, LOW); 
    delay(10);                       
    int rawBat = analogRead(PIN_VBAT);
    digitalWrite(VBAT_ENABLE, HIGH); 

    int batPercent = map(rawBat, BAT_MIN_RAW, BAT_MAX_RAW, 0, 100);
    batPercent = constrain(batPercent, 0, 100);

    if (batPercent >= 90) {
      globalSat = 0;   
      globalHue = 0;   
    } else {
      globalSat = 255; 
      globalHue = map(batPercent, 0, 90, 0, 96); 
    }
  }

  // ==========================================
  // 5. LED UPDATES WITH SMOOTHING & EXACT MAPPINGS
  // ==========================================
  FastLED.clear(); // We overwrite all mapped LEDs, clearing prevents artifacts

  // Loop through electrodes 0 to 9
  for (int i = 0; i < 10; i++) {
    float target_b = 0;
    
    // Read the delta for this specific electrode
    if (ele_delta[i] > 5) {
      int mapped_b = map(ele_delta[i], 0, 100, 0, 255);
      target_b = constrain(mapped_b, 0, 255);
    }
    
    // This is the smoothing logic. 
    // 0.02 is a very low factor, making the LEDs take seconds to reach maximum brightness. 
    // Change 0.02 to a higher number (like 0.1) if you want it faster.
    currentBrightness[i] += (target_b - currentBrightness[i]) * 0.02;

    // Apply the smoothed brightness to the assigned LEDs
    uint8_t final_b = (uint8_t)currentBrightness[i];
    
    for(int j = 0; j < led_sizes[i]; j++) {
      int led_index = led_maps[i][j];
      leds[led_index] = CHSV(globalHue, globalSat, final_b);
    }
  }
  
  FastLED.show();
  delay(20);  
}

// ==========================================
// MPR121 HARDWARE FUNCTIONS
// ==========================================

void MPR121_write_reg(uint8_t reg, uint8_t value) {
  if (!si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0)) {
    si.i2c_stop();
    return;
  }
  si.i2c_write(reg);
  si.i2c_write(value);
  si.i2c_stop();
}

bool MPR121_read_block(uint8_t startReg, uint8_t *buffer, uint8_t length) {
  if (!si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0)) {
    si.i2c_stop();
    return false;
  }
  si.i2c_write(startReg);

  if (!si.i2c_rep_start((MPR121_I2C_ADDRESS << 1) | 1)) {
    si.i2c_stop();
    return false;
  }

  for (uint8_t i = 0; i < length; i++) {
    bool isLastByte = (i == (length - 1));
    buffer[i] = si.i2c_read(isLastByte);
  }

  si.i2c_stop();
  return true;
}

void MPR121_init_arduino() {
  MPR121_write_reg(0x80, 0x63);  
  MPR121_write_reg(0x5E, 0x00);  
  delay(1);                      
  MPR121_write_reg(0x80, 0x63);  
  delay(10);                     
  MPR121_write_reg(0x2B, 0x01);  
  MPR121_write_reg(0x2C, 0x01);  
  MPR121_write_reg(0x2D, 0x00);  
  MPR121_write_reg(0x2E, 0x00);  
  MPR121_write_reg(0x2F, 0x01);  
  MPR121_write_reg(0x30, 0x01);  
  MPR121_write_reg(0x31, 0xFF);  
  MPR121_write_reg(0x32, 0x02);  
  MPR121_write_reg(0x33, 0x00);  
  MPR121_write_reg(0x34, 0x00);  
  MPR121_write_reg(0x35, 0x00);  

  for (uint8_t i = 0; i < 12; i++) {
    MPR121_write_reg(0x41 + i * 2, TouchThre);  
    MPR121_write_reg(0x42 + i * 2, ReleaThre);  
  }

  MPR121_write_reg(0x5C, 0x40);  
  MPR121_write_reg(0x5D, 0x00);  
  MPR121_write_reg(0x5B, (1 << 4) | (1 << 0));  
  MPR121_write_reg(0x7B, 0xCB);  
  MPR121_write_reg(0x7C, 0x00);  
  MPR121_write_reg(0x7D, 0xE4);  
  MPR121_write_reg(0x7E, 0x94);  
  MPR121_write_reg(0x7F, 0xCD);  
  MPR121_write_reg(0x5E, 0x0C);  
}

void Read_MPR121_ele_register_arduino() {
  if (!MPR121_read_block(0x00, readingArray, MPR121_READ_LENGTH)) {
    Serial.println("Failed to read MPR121");
  }
}

void Get_ele_data_arduino() {
  uint16_t tmp_sig, tmp_bas;
  for (uint8_t i = 0; i < 13; i++) {  
    tmp_sig = (readingArray[0x04 + (2 * i)] | ((uint16_t)readingArray[0x05 + (2 * i)] << 8));
    tmp_sig &= 0xFFFC;
    tmp_bas = ((uint16_t)readingArray[0x1E + i]) << 2;
    ele_delta[i] = abs((int16_t)(tmp_sig - tmp_bas));
  }
}

void Get_touch_status_arduino() {
  CurrTouchStatus.Reg0 = readingArray[0x00];  
  CurrTouchStatus.Reg1 = readingArray[0x01];  

  if (((CurrTouchStatus.Reg0 & 0xFF) != 0) || ((CurrTouchStatus.Reg1 & 0x0F) != 0)) {  
    CurrTouchStatus.Touched = true;
  } else {
    CurrTouchStatus.Touched = false;
  }
}