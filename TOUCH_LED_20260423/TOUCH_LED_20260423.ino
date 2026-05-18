#include "Adafruit_TinyUSB.h"
#include <SlowSoftI2CMaster.h>
#include <FastLED.h>

// --- LED CONFIGURATION ---
#define NUM_LEDS 28
#define DATA_PIN D10
CRGB leds[NUM_LEDS];

// --- BEHAVIOR SETTINGS ---
#define SLEEP_TIMEOUT   10000 // 10 seconds of inactivity to sleep
#define WAKE_HOLD_TIME  1000  // Must hold ELE7 for 1 second (1000ms) to wake
#define FADE_IN_SPEED   15    // How fast LEDs brighten (1-255)
#define FADE_OUT_SPEED  5     // How fast LEDs fade out (1-255)

// --- STATE VARIABLES ---
unsigned long lastActivityTime = 0;
unsigned long wakeTouchStart = 0;
bool isAsleep = false;
bool wasTouchingEle7 = false;
uint8_t eleBrightness[10] = {0}; // Tracks the current brightness of each group

// --- MPR121 CONFIGURATION ---
#define MPR121_I2C_ADDRESS 0x5A  
#define MPR121_READ_LENGTH 0x2B  
#define TouchThre 6  
#define ReleaThre 4  

uint8_t readingArray[MPR121_READ_LENGTH];
uint16_t touchData = 0;

struct TouchStatus {
  uint8_t Reg0;
  uint8_t Reg1;
  bool Touched;  
};

TouchStatus CurrTouchStatus;
SlowSoftI2CMaster si = SlowSoftI2CMaster(D5, D4, true);

// --- EXACT LED MAPPINGS ---
const int ele0_leds[] = {23, 24, 25, 26}; const int sz0 = 4;
const int ele1_leds[] = {27, 16, 17, 18}; const int sz1 = 4;
const int ele2_leds[] = {13, 14, 15};     const int sz2 = 3;
const int ele3_leds[] = {1, 2};           const int sz3 = 2;
const int ele4_leds[] = {5, 6};           const int sz4 = 2;
const int ele5_leds[] = {10, 11, 12};     const int sz5 = 3;
const int ele6_leds[] = {3, 4};           const int sz6 = 2;
const int ele7_leds[] = {0};              const int sz7 = 1; // Wake Up Pin
const int ele8_leds[] = {7, 8, 9};        const int sz8 = 3;
const int ele9_leds[] = {19, 20, 21, 22}; const int sz9 = 4;


void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();

  if (!si.i2c_init()) {
    while(1);
  }

  MPR121_init_arduino();
  CurrTouchStatus.Touched = false; 
  lastActivityTime = millis();
}

void loop() {
  // 1. READ SENSOR DATA
  Read_MPR121_ele_register_arduino();  
  Get_touch_status_arduino();          
  touchData = CurrTouchStatus.Reg0 | ((uint16_t)(CurrTouchStatus.Reg1 & 0x0F) << 8);

  // 2. SLEEP / WAKE LOGIC
  if (isAsleep) {
    if (touchData & (1 << 7)) { // Touching ELE7
      if (!wasTouchingEle7) {
        wakeTouchStart = millis(); // Start the hold timer
        wasTouchingEle7 = true;
      } else if (millis() - wakeTouchStart > WAKE_HOLD_TIME) {
        isAsleep = false; // WAKE UP!
        lastActivityTime = millis();
        wasTouchingEle7 = false;
      }
    } else {
      wasTouchingEle7 = false; // Reset timer if let go early
    }
    
    // Ensure LEDs are off while asleep and skip the rest of the loop
    FastLED.clear();
    FastLED.show();
    delay(15);
    return; 
  }

  // 3. INACTIVITY TIMEOUT
  if (touchData > 0) {
    lastActivityTime = millis(); // Reset sleep timer if anything is touched
  } else if (millis() - lastActivityTime > SLEEP_TIMEOUT) {
    isAsleep = true;
    for (int i = 0; i < 10; i++) eleBrightness[i] = 0; // Reset all brightness trackers
    return;
  }

  // 4. PROCESS FADING
  FastLED.clear(); 

  // Process each electrode group
  updateGroup(0, touchData & (1 << 0), ele0_leds, sz0, sz0, 100);   // Red
  updateGroup(1, touchData & (1 << 1), ele1_leds, sz1, sz1, 100);  // Orange
  updateGroup(2, touchData & (1 << 2), ele2_leds, sz2, sz2, 100);  // Yellow
  updateGroup(3, touchData & (1 << 3), ele3_leds, sz3, sz3, 100);  // Green
  updateGroup(4, touchData & (1 << 4), ele4_leds, sz4, sz4, 100); // Aqua
  updateGroup(5, touchData & (1 << 5), ele5_leds, sz5, sz5, 100); // Blue
  updateGroup(6, touchData & (1 << 6), ele6_leds, sz6, sz6, 100); // Purple
  updateGroup(7, touchData & (1 << 7), ele7_leds, sz7, sz7, 100); // Pink
  updateGroup(8, touchData & (1 << 8), ele8_leds, sz8, sz8, 100);     // White (Sat 0)
  updateGroup(9, touchData & (1 << 9), ele9_leds, sz9, sz9, 100);  // Pale Yellow

  // 5. PUSH TO STRIP
  FastLED.show();
  delay(15); 
}

// --- FADING HELPER FUNCTION ---
void updateGroup(int groupIdx, bool isTouched, const int* ledsArray, int size, uint8_t hue, uint8_t sat) {
  // Attack (Fade In)
  if (isTouched) {
    if (eleBrightness[groupIdx] <= 255 - FADE_IN_SPEED) {
      eleBrightness[groupIdx] += FADE_IN_SPEED;
    } else {
      eleBrightness[groupIdx] = 255;
    }
  } 
  // Decay (Fade Out)
  else {
    if (eleBrightness[groupIdx] >= FADE_OUT_SPEED) {
      eleBrightness[groupIdx] -= FADE_OUT_SPEED;
    } else {
      eleBrightness[groupIdx] = 0;
    }
  }

  // Apply calculated brightness to the specific LEDs
  if (eleBrightness[groupIdx] > 0) {
    for (int i = 0; i < size; i++) {
      leds[ledsArray[i]] = CHSV(hue, sat, eleBrightness[groupIdx]);
    }
  }
}

// ==============================================================================
// MPR121 HARDWARE FUNCTIONS (Intact from stable code)
// ==============================================================================

void MPR121_write_reg(uint8_t reg, uint8_t value) {
  if (!si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0)) { si.i2c_stop(); return; }
  si.i2c_write(reg); si.i2c_write(value); si.i2c_stop();
}

bool MPR121_read_block(uint8_t startReg, uint8_t *buffer, uint8_t length) {
  if (!si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0)) { si.i2c_stop(); return false; }
  si.i2c_write(startReg);
  if (!si.i2c_rep_start((MPR121_I2C_ADDRESS << 1) | 1)) { si.i2c_stop(); return false; }
  for (uint8_t i = 0; i < length; i++) {
    buffer[i] = si.i2c_read(i == (length - 1));
  }
  si.i2c_stop(); return true;
}

void MPR121_init_arduino() {
  MPR121_write_reg(0x80, 0x63); delay(10);
  MPR121_write_reg(0x5E, 0x00); delay(1);
  MPR121_write_reg(0x2B, 0x01); MPR121_write_reg(0x2C, 0x01); 
  MPR121_write_reg(0x2D, 0x00); MPR121_write_reg(0x2E, 0x00); 
  MPR121_write_reg(0x2F, 0x01); MPR121_write_reg(0x30, 0x01); 
  MPR121_write_reg(0x31, 0xFF); MPR121_write_reg(0x32, 0x02); 
  MPR121_write_reg(0x33, 0x00); MPR121_write_reg(0x34, 0x00); 
  MPR121_write_reg(0x35, 0x00); 
  for (uint8_t i = 0; i < 12; i++) {
    MPR121_write_reg(0x41 + i * 2, TouchThre);  
    MPR121_write_reg(0x42 + i * 2, ReleaThre);  
  }
  MPR121_write_reg(0x5C, 0x40); MPR121_write_reg(0x5D, 0x00);  
  MPR121_write_reg(0x5B, (1 << 4) | (1 << 0));  
  MPR121_write_reg(0x7B, 0xCB); MPR121_write_reg(0x7C, 0x00); 
  MPR121_write_reg(0x7D, 0xE4); MPR121_write_reg(0x7E, 0x94); 
  MPR121_write_reg(0x7F, 0xCD); 
  MPR121_write_reg(0x5E, 0x0C);  
}

void Read_MPR121_ele_register_arduino() {
  MPR121_read_block(0x00, readingArray, MPR121_READ_LENGTH);
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