#include <SlowSoftI2CMaster.h>
#include "Adafruit_TinyUSB.h"
#include <FastLED.h>
#include <bluefruit.h>

// Device Information Service
BLEDis bledis;
// BLE UART Service (this automatically uses the exact NUS UUIDs your web app looks for)
BLEUart bleuart;

// How many leds in your strip?
#define NUM_LEDS 28

// For led chips like WS2812, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
// Clock pin only needed for SPI based chipsets when not using hardware SPI
#define DATA_PIN D10
#define CLOCK_PIN 13

// Define the array of leds
CRGB leds[NUM_LEDS];

#define MPR121_I2C_ADDRESS 0x5A  // Default MPR121 Address 0x5A

// Define one of these filter types, or 0 for no specific filter logic in Pol_mouse_dat
#define FILTER_NONE 0
#define FILTER_G 1  // Example: Gaussian-like filter
#define FILTER_D 2  // Example: Derivative-like filter
#define FILTER_A 3  // Example: Another filter type

#define CURRENT_FILTER FILTER_G  // CHOOSE YOUR FILTER TYPE HERE

// Sensitivity thresholds for mouse movement - TUNE THESE!
// These are used in the DX/DY scaling part of Pol_mouse_dat_arduino
#define S_X 20  // Example: Increased from 20 due to larger coordinate range from GetPosXY
#define S_Y 20  // Example: Increased from 20

// Touch and Release Thresholds (as in original code)
// More sensitive (adjust these values as needed):
#define TouchThre 6  // Lower value means easier to trigger touch
#define ReleaThre 4  // Keep release slightly lower than touch


// --- Global Variables ---
// Buffer to store raw register data from MPR121
// Registers 0x00 to 0x2A (inclusive) = 43 bytes
#define MPR121_READ_LENGTH 0x2B  // 43 bytes
uint8_t readingArray[MPR121_READ_LENGTH];

// Delta values for each electrode (signal - baseline)
int16_t ele_delta[13];  // We'll calculate 13, use 12 for X/Y
bool hasKeyPressed = false;

int touchDX, touchDY;

// Touch status structure
struct TouchStatus {
  uint8_t Reg0;
  uint8_t Reg1;
  bool Touched;  // Using bool for Arduino
};


TouchStatus CurrTouchStatus;
TouchStatus PreTouchStatus = { 0, 0, false };  // Initialize previous state


uint8_t FstFlg = 0;   // Flag for ignoring initial samples
bool SndFlg = false;  // Another flag used in filter logic


// use true as third arg, if you do not have external pullups
SlowSoftI2CMaster si = SlowSoftI2CMaster(D5, D4, true);

void setup() {


  Serial.begin(9600);
  while (!Serial)
    ;  // Leonardo: wait for serial monitor
  Serial.println("\nMPR Test");

  if (!si.i2c_init())
    Serial.println(F("Initialization error. SDA or SCL are low"));
  else
    Serial.println(F("...done"));
  MPR121_init_arduino();
  PreTouchStatus.Touched = false;  // Initialize previous states


  Serial.println("\nLEDs Test");
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed

  for (int ledN = 0; ledN < NUM_LEDS; ledN++) {
    // Turn the LED on, then pause
    leds[ledN] = CRGB::Red;
    FastLED.show();
    delay(100);
  }
  for (int ledN = NUM_LEDS; ledN >= 0; ledN--) {
    // Turn the LED on, then pause
    leds[ledN] = CRGB::Black;
    FastLED.show();
    delay(10);
  }


  // Initialize Bluefruit with 1 peripheral connection, 0 central connections
  Bluefruit.begin(1, 0);
  Bluefruit.setTxPower(4);
  Bluefruit.setName("XIAO nRF52840");

  // Configure and start Device Information Service
  bledis.setManufacturer("Seeed Studio");
  bledis.setModel("XIAO nRF52840");
  bledis.begin();

  // Configure and start BLE UART service
  bleuart.begin();

  // Set up and start advertising
  startAdv();
}

void loop() {

  Read_MPR121_ele_register_arduino();  // Reads all relevant registers into readingArray
  Get_ele_data_arduino();              // Calculates ele_delta from readingArray
  Get_touch_status_arduino();          // Updates CurrTouchStatus from readingArray


  // Optional: Print detailed electrode delta data
  // if (CurrTouchStatus.Touched) {
  // Serial.print();
  for (int k = 3; k < 12; k++) {
    Serial.print(ele_delta[k]);
    Serial.print(",");
    float b = 0;
    if (ele_delta[k] > 5) {
      b = map(ele_delta[k], 0, 100, 0, 255);
    }
    leds[2 * (k - 3)] = CRGB(b, 0, b);
    leds[2 * (k - 3) + 1] = CRGB(b, 0, b);
    FastLED.show();
  }
  Serial.println("END");
  // }

  // Serial.print(touchDX);
  // Serial.print(',');
  // Serial.println(touchDY);

  // If not connected to the web app, do nothing
  if (!Bluefruit.connected()) {
    return;
  }

  // Periodically send a ping to the Web App (every 2 seconds)
  static unsigned long lastTime = 0;
  if (millis() - lastTime > 2000) {
    lastTime = millis();

    String msg = "Ping from XIAO: " + String(millis());

    // Print directly acts over the TX characteristic
    bleuart.print(msg);
    for (int k = 3; k < 12; k++) {
      bleuart.print(ele_delta[k]);
      bleuart.print(",");
    }
    bleuart.println();
    Serial.println("Sent to Web: " + msg);
  }

  // Check for incoming data from the Web App (RX characteristic)
  if (bleuart.available()) {
    Serial.print("Received from Web: ");
    while (bleuart.available()) {
      Serial.print((char)bleuart.read());
    }
    Serial.println();
  }


  delay(20);  // Adjust delay as needed for responsiveness vs. processing load (50Hz)
              // Consider non-blocking timing if other tasks are needed.
}


void MPR121_write_reg(uint8_t reg, uint8_t value) {
  // 1. Send START condition and Address in WRITE mode (Bit shifted + 0)
  if (!si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0)) {
    Serial.print("I2C Write Start Error! Reg: 0x");
    Serial.println(reg, HEX);
    si.i2c_stop();
    return;
  }

  // 2. Send Register and Value immediately
  si.i2c_write(reg);
  si.i2c_write(value);

  // 3. Send STOP condition to release the bus
  si.i2c_stop();
}


bool MPR121_read_block(uint8_t startReg, uint8_t* buffer, uint8_t length) {
  // 1. Point to the register we want to read (Requires WRITE mode first)
  if (!si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0)) {
    Serial.println("I2C Read Setup Error (Start)!");
    si.i2c_stop();
    return false;
  }
  si.i2c_write(startReg);

  // 2. Send REPEATED START condition and switch to READ mode (Bit shifted + 1)
  if (!si.i2c_rep_start((MPR121_I2C_ADDRESS << 1) | 1)) {
    Serial.println("I2C Read Setup Error (Repeated Start)!");
    si.i2c_stop();
    return false;
  }

  // 3. Read the incoming bytes one by one
  for (uint8_t i = 0; i < length; i++) {
    // The i2c_read function needs to know if this is the final byte.
    // We pass 'true' on the last byte to send a NACK (telling the sensor to stop).
    // We pass 'false' on all other bytes to send an ACK (asking for the next byte).
    bool isLastByte = (i == (length - 1));
    buffer[i] = si.i2c_read(isLastByte);
  }

  // 4. Send STOP condition
  si.i2c_stop();
  return true;
}

// --- MPR121 Initialization and Data Processing (Translated) ---
void MPR121_init_arduino() {

  // here there are parameters to adjust, since I used Gemini to translate the i2c routines and it changed some numbers

  Serial.println("Initializing MPR121 Touch sensor");


  MPR121_write_reg(0x80, 0x63);  //Soft reset


  // Stop mode to allow register changes
  MPR121_write_reg(0x5E, 0x00);  // ELECONF: Enter Stop mode
  delay(1);                      // Short delay after stop

  // Soft reset (optional, but good practice if unsure about initial state)
  MPR121_write_reg(0x80, 0x63);  // SOFT_RESET
  delay(10);                     // Allow time for reset

  // Baseline filtering (Original values from your code)
  MPR121_write_reg(0x2B, 0x01);  // MHD_R: Max Half Delta Rising
  MPR121_write_reg(0x2C, 0x01);  // NHD_R: Noise Half Delta Rising
  MPR121_write_reg(0x2D, 0x00);  // NCL_R: Noise Count Limit Rising
  MPR121_write_reg(0x2E, 0x00);  // FDL_R: Filter Delay Limit Rising

  MPR121_write_reg(0x2F, 0x01);  // MHD_F: Max Half Delta Falling
  MPR121_write_reg(0x30, 0x01);  // NHD_F: Noise Half Delta Falling
  MPR121_write_reg(0x31, 0xFF);  // NCL_F: Noise Count Limit Falling
  MPR121_write_reg(0x32, 0x02);  // FDL_F: Filter Delay Limit Falling

  MPR121_write_reg(0x33, 0x00);  // NHD_T: Noise Half Delta Touched (Proximity related)
  MPR121_write_reg(0x34, 0x00);  // NCL_T: Noise Count Limit Touched (Proximity related)
  MPR121_write_reg(0x35, 0x00);  // FDL_T: Filter Delay Limit Touched (Proximity related)

  // Touch/Release Thresholds for all 12 channels
  for (uint8_t i = 0; i < 12; i++) {
    MPR121_write_reg(0x41 + i * 2, TouchThre);  // ELEi_TouchThreshold
    MPR121_write_reg(0x42 + i * 2, ReleaThre);  // ELEi_ReleaseThreshold
  }

  // AFE (Analog Front End) Configuration - INCREASED SENSITIVITY
  // Original from your code: MPR121_write_reg(0x5C, 0xC0); // FFI=3 (1ms), CDC=0 (1uA) - VERY LOW SENSITIVITY
  MPR121_write_reg(0x5C, 0x40);  // New: FFI=0 (0.5ms sample interval), CDC=32uA.
                                 // Try 0x10 for 16uA, or 0x2F for 62uA if needed.
                                 // Values for CDC (bits 5-0): 0x01=1uA, 0x10=16uA, 0x20=32uA, 0x2F=63uA

  MPR121_write_reg(0x5D, 0x00);  // AFE_CONF2: SFI=0 (4 samples), ESI=0 (1ms sample interval) - Default from your code
                                 // ESI (bits 5-0): 0x00=1ms, 0x01=0.5ms. FFI and ESI combine for overall sample rate.

  // Debounce for touch and release (DT and DR bits)
  // Bits 6-4 for DT (touch debounce), Bits 2-0 for DR (release debounce)
  // Value 1 = 1 sample, Value 2 = 2 samples etc.
  MPR121_write_reg(0x5B, (1 << 4) | (1 << 0));  // Debounce: Touch=1 sample, Release=1 sample. (Sensitive)
                                                // Try (2 << 4) | (2 << 0) for 2 samples if too noisy.

  // Auto-Configuration Registers (Original values from your code)
  // These fine-tune the auto-calibration behavior.
  MPR121_write_reg(0x7B, 0xCB);  // AUTO_CONFIG_0 (USL, Target Level related for ELE0-ELE3)
  MPR121_write_reg(0x7C, 0x00);  // AUTO_CONFIG_0 (LSL, Target Level related for ELE0-ELE3) - This was not in your list, typically set. Default may be 0.
                                 // Default for 0x7C by Adafruit library is often 0x8C or LSL = USL * 0.65
  MPR121_write_reg(0x7D, 0xE4);  // AUTO_CONFIG_1 (USL, Target Level related for ELE4-ELE11)
  MPR121_write_reg(0x7E, 0x94);  // AUTO_CONFIG_CONTROL_0 (SFI, RETRY, BVA, ARE, ACE)
  MPR121_write_reg(0x7F, 0xCD);  // AUTO_CONFIG_CONTROL_1 (SCTS, ACFAILI, CL)


  // Enable electrodes and enter Run mode (12 Electrodes)
  MPR121_write_reg(0x5E, 0x0C);  // ELECONF: Enable 12 electrodes (ELE0-ELE11), Target: Run mode

  // Thresholds for the 13th Proximity Electrode (Registers 0x59 and 0x5A)
  // MPR121_write_reg(0x59, 10);  // ELE12 (Proximity) Touch Threshold
  // MPR121_write_reg(0x5A, 8);   // ELE12 (Proximity) Release Threshold


  Serial.println("MPR121 Initialized.");
}

void Read_MPR121_ele_register_arduino() {
  // Read registers 0x00 to 0x2A (43 bytes)
  if (!MPR121_read_block(0x00, readingArray, MPR121_READ_LENGTH)) {
    Serial.println("Failed to read MPR121 registers!");
  }
}

void Get_ele_data_arduino() {
  uint16_t tmp_sig, tmp_bas;
  for (uint8_t i = 0; i < 13; i++) {  // Process 13 values (ELE0-ELE11 and PROX/ELE12)
    // Filtered data: LSB at 0x04 + 2*i, MSB at 0x05 + 2*i
    // The original `readingArray[0x04 + 1 + 2*i]` is MSB, `readingArray[0x04 + 2*i]` is LSB
    tmp_sig = (readingArray[0x04 + (2 * i)] | ((uint16_t)readingArray[0x05 + (2 * i)] << 8));
    // MPR121 Filtered data is 10-bit. The mask 0xFFFC in original code seems to ignore 2 LSBs.
    // Sticking to original mask:
    tmp_sig &= 0xFFFC;

    // Baseline data: 1 byte per channel (0x1E + i), value is upper 8 bits of 10-bit baseline.
    tmp_bas = ((uint16_t)readingArray[0x1E + i]) << 2;

    ele_delta[i] = abs((int16_t)(tmp_sig - tmp_bas));
  }
}

void Get_touch_status_arduino() {
  CurrTouchStatus.Reg0 = readingArray[0x00];  // Touch Status LSB (ELE0-ELE7)
  CurrTouchStatus.Reg1 = readingArray[0x01];  // Touch Status MSB (ELE8-ELE11, plus OOR and PROX)

  // Check if any of the first 12 electrodes are touched
  if (((CurrTouchStatus.Reg0 & 0xFF) != 0) || ((CurrTouchStatus.Reg1 & 0x0F) != 0)) {  // Mask MSB for ELE8-11
    CurrTouchStatus.Touched = true;
  } else {
    CurrTouchStatus.Touched = false;
  }
}


void startAdv(void) {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include the BLE UART service UUID (128-bit)
  Bluefruit.Advertising.addService(bleuart);

  // Because the 128-bit UART Service UUID takes up a lot of room in the primary
  // advertising packet, we put the device Name in the Scan Response packet instead.
  Bluefruit.ScanResponse.addName();

  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);  // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);    // number of seconds in fast mode
  Bluefruit.Advertising.start(0);              // 0 = Don't stop advertising after n seconds
}
