
/*
 VFO/BFO modified for a different display and modified for a different uC.
 forked from Ricardo Lima Caratti (PU2CLR) https://github.com/pu2clr/VFO_BFO_OLED_ARDUINO/blob/master/source/si5351_vfobfo.ino 
*/

#include <si5351.h>
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"

// Enconder PINs
#define ENCODER_PIN_A D4 // 
#define ENCODER_PIN_B D3 // 

// OLED Diaplay constants
#define I2C_ADDRESS 0x3C
#define RST_PIN -1 // Define proper RST_PIN if required.

// Change this value below  (CORRECTION_FACTOR) to 0 if you do not know the correction factor of your Si5351A.
#define CORRECTION_FACTOR 0 // See how to calibrate your Si5351A (0 if you do not want).

#define BUTTON_STEP D7    // Control the frequency increment and decrement
#define BUTTON_BAND_UP D6    // Controls the band
#define BUTTON_BAND_DN D5 // Switch VFO to BFO

// BFO range for this project is 400KHz to 500KHz. The central frequency is 455KHz.
#define MAX_BFO 5000000000LU    // BFO maximum frequency
#define CENTER_BFO 0LU // BFO centeral frequency - set to zero for direct conversion
#define MIN_BFO 0LU    // BFO minimum frequency

#define MIN_ELAPSED_TIME 300

// OLED - Declaration for a SSD1306 display connected to I2C (SDA, SCL pins)
SSD1306AsciiWire display;

// The Si5351 instance.
Si5351 si5351;

// Structure for Bands database
typedef struct
{
  char *name;
  uint64_t minFreq; // Min. frequency value for the band (unit 0.01Hz)
  uint64_t maxFreq; // Max. frequency value for the band (unit 0.01Hz)
} Band;

// Band database. You can change the band ranges if you need.
// The unit of frequency here is 0.01Hz (1/100 Hz). See Etherkit Library at https://github.com/etherkit/Si5351Arduino
Band band[] = {
    {"AM bc", 65000000LLU, 170000000LLU},     // AM broadcast
    {"160m ", 170000000LLU, 200000000LLU},    // 160m Ham band
    {"80mT ", 352500000LLU, 360000001LLU},    // 80m tech portion
    {"80m  ", 350000000LLU, 400000001LLU},    // 80m ham band
    {"SW1  ", 400000000LLU, 700000000LLU},
    {"40mT ", 702500000LLU, 712500000LLU},    // 40m tech portion
    {"40m  ", 700000000LLU, 730000000LLU},    // 40m ham band
    {"SW2  ", 730000000LLU, 1010000000LLU},   
    {"30m  ", 1010000000LLU, 1015000000LLU},  // 30m ham band
    {"SW3  ", 1015000000LLU, 1400000000LLU}, 
    {"20m  ", 1400000000LLU, 1435000000LLU},  // 20m ham band
    {"SW4  ", 1435000000LLU, 1806800000LLU}, 
    {"17m  ", 1806800000LLU, 1816800000LLU},  // 17m
    {"SW5  ", 1816800000LLU, 2100000000LLU},
    {"15mT ", 2125000000LLU, 2120000000LLU},  // 15m tech portion
    {"15m  ", 2100000000LLU, 2145000000LLU},  // 15m
    {"SW6  ", 2145000000LLU, 2498000000LLU},
    {"12m  ", 2488000000LLU, 2499000000LLU},  // 12m
    {"SW7  ", 2499000000LLU, 2600000000LLU},
    {"cb   ", 2600000000LLU, 2800000000LLU},
    {"10mT ", 2800000000LLU, 2850000000LLU},  // 10m tech area
    {"10m  ", 2800000000LLU, 2970000000LLU},  // 10m general coverage
    {"VHF 1", 2970000000LLU, 5000000000LLU}, 
    {"6m   ", 5000000000LLU, 5400000000LLU},  // 6m ham band
    {"VHF2 ", 5400000000LLU, 8600000000LLU},
    {"FM bc", 8600000000LLU, 10800000000LLU}, // Comercial FM
    {"VHF3 ", 10800000000LLU, 12000000000LLU}, 
    {"VHF4 ", 12000000000LLU, 14400000000LLU},// commercial/air band
    {"2m   ", 14400000000LLU, 14800000000LLU},// 2m ham band
    {"VHF5 ", 14800000000LLU, 16000000000LLU} // police/fire/pagers/etc
};

// Calculate the last element position (index) of the array band
const int lastBand = (sizeof band / sizeof(Band)) - 1; // For this case will be 26.
const int firstBand = 0;                               // added for consistency
volatile int currentBand = 1;                          // First band. For this case, AM is the current band.

// Struct for step database
typedef struct
{
  char *name; // step label: 50Hz, 100Hz, 500Hz, 1KHz, 5KHz, 10KHz and 500KHz
  long value; // Frequency value (unit 0.01Hz See documentation) to increment or decrement
} Step;

// Steps database. You can change the Steps and numbers of steps here if you need.
Step step[] = {
    {"10Hz  ", 1000}, // VFO and BFO min. increment / decrement
    {"50Hz  ", 5000},
    {"100Hz ", 10000},
    {"500Hz ", 50000},
    {"1KHz  ", 100000}, // BFO max. increment / decrement
    {"2.5KHz", 250000},
    {"5KHz  ", 500000},
    {"10KHz ", 1000000},
    {"100KHz", 10000000},
    {"500KHz", 50000000},
    {"1mHz", 100000000}
    }; // VFO max. increment / decrement
// Calculate the index of last position of step[] array (in this case will be 8)
const int lastStepVFO = (sizeof step / sizeof(Step)) - 1; // index for max increment / decrement for VFO
volatile int lastStepBFO = 11;                             // index for max. increment / decrement for BFO. 
volatile long currentStep = 4;                            // it stores the current step index 

volatile boolean isFreqChanged = false;
volatile boolean clearDisplay = false;

// LW/MW is the default band
volatile uint64_t vfoFreq = band[currentBand].minFreq; //
volatile uint64_t bfoFreq = CENTER_BFO;                // 455 KHz for this project
// VFO is the Si5351A CLK0
// BFO is the Si5351A CLK2
volatile int currentClock = 0; // If 0, then VFO will be controlled else the BFO will be

long volatile elapsedTimeInterrupt = millis(); // will control the minimum time to process an interrupt action
long elapsedTimeEncoder = millis();

// Encoder variable control
unsigned char encoder_pin_a;
unsigned char encoder_prev = 0;
unsigned char encoder_pin_b;

void setup()
{
  Wire.begin();
  // Encoder pins
  pinMode(ENCODER_PIN_A, INPUT);
  pinMode(ENCODER_PIN_B, INPUT);
  // Si5351 contrtolers pins
  pinMode(BUTTON_BAND_UP, INPUT);
  pinMode(BUTTON_STEP, INPUT);
  pinMode(BUTTON_BAND_DN, INPUT);
  
  // Initiating the OLED Display
  display.begin(&Adafruit128x64, I2C_ADDRESS);
//  display.setFont(Adafruit5x7);
//  display.set2X();
  display.set400kHz();
  display.clear();
  display.print("\n KC8IJQ");
  delay(3000);
  display.clear();
  displayDial();
  // Initiating the Signal Generator (si5351)
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  // Adjusting the frequency (see how to calibrate the Si5351 - example si5351_calibration.ino)
  si5351.set_correction(CORRECTION_FACTOR);
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
  si5351.set_freq(vfoFreq, SI5351_CLK0); // Start CLK0 (VFO)
  si5351.set_freq(bfoFreq, SI5351_CLK2); // Start CLK2 (BFO)
  si5351.update_status();
  // Show the initial system information
  delay(500);

  // Will stop what Arduino is doing and call changeStep(), changeBand() or switchVFOBFO
  attachInterrupt(digitalPinToInterrupt(BUTTON_STEP), changeStep, RISING);      // whenever the BUTTON_STEP goes from LOW to HIGH
  attachInterrupt(digitalPinToInterrupt(BUTTON_BAND_UP), changeBandUp, RISING);      // whenever the BUTTON_BAND_UP goes from LOW to HIGH
  attachInterrupt(digitalPinToInterrupt(BUTTON_BAND_DN), changeBandDn, RISING); // whenever the BUTTON_BAND_DN goes from LOW to HIGH
  // wait for 1/2 second and the system will be ready.
  delay(500);
}


// Show Signal Generator Information
// Verificar setCursor() em https://github.com/greiman/SSD1306Ascii/issues/53
void displayDial()
{
  double vfo = vfoFreq / 100000000.0;
  double bfo = bfoFreq / 100000000.0;
  String mainFreq;
  String secoundFreq;
  String staticFreq;
  String dinamicFreq;

  // Change the display behaviour depending on who is controlled, BFO or BFO.
  if (currentClock == 0)
  { // If the encoder is controlling the VFO
    mainFreq = String(vfo,3);
    secoundFreq = String(bfo,3);
    staticFreq = "BFO";
    dinamicFreq = "VFO";
  }
  else // encoder is controlling the VFO
  {
    mainFreq = String(bfo,3);
    secoundFreq = String(vfo,3);
    staticFreq = "VFO";
    dinamicFreq = "BFO";
  }
/*
  // display.setCursor(0,0)
  //display.clear();

  display.set2X();
  display.setCursor(0, 0);
  display.print(mainFreq);
  display.print("     ");

  display.set1X();
  display.print("\n\n\n");
  display.print(staticFreq);
  display.print(".: ");
  display.print(secoundFreq);

  display.print("\nBand: ");
  display.print(band[currentBand].name);

  display.print("\nStep: ");
  display.print(step[currentStep].name);

  display.print("\n\nCtrl: ");
  display.print(dinamicFreq);
*/
///*
  display.setFont(System5x7);
  display.clear();
  display.set2X();
  display.println("MiniVFO");
  display.println(mainFreq);
  display.set1X();
  //display.println("");
  display.print("band:");
  display.print(band[currentBand].name);
  display.print("step:");
  display.println(step[currentStep].name);
  display.print("vfo/bfo adjust:");
  display.println(dinamicFreq); 
  //*/

}

// Change the frequency (increment or decrement)
// direction parameter is 1 (clockwise) or -1 (counter-clockwise)
void changeFreq(int direction)
{
  if (currentClock == 0)
  { // Check who will be chenged (VFO or BFO)
    vfoFreq += step[currentStep].value * direction;
    // Check the VFO limits
    if (vfoFreq > band[currentBand].maxFreq) // Max. VFO frequency for the current band
    {
      vfoFreq = band[currentBand].minFreq; // Go to min. frequency of the range
    }
    else if (vfoFreq < band[currentBand].minFreq) // Min. VFO frequency for the band
    {
      vfoFreq = band[currentBand].maxFreq; // Go to max. frequency of the range
    }
  }
  else
  {
    bfoFreq += step[currentStep].value * direction; // currentStep * direction;
    // Check the BFO limits
    if (bfoFreq > MAX_BFO || bfoFreq < MIN_BFO) // BFO goes to center if it is out of the limits
    {
      bfoFreq = CENTER_BFO;     // Go to center
    }
  }
  isFreqChanged = true;
}

// Change frequency increment rate
void changeStep()
{
  if ((millis() - elapsedTimeInterrupt) < MIN_ELAPSED_TIME)
    return;                                                            // nothing to do if the time less than MIN_ELAPSED_TIME milisecounds
  noInterrupts();                                                      //// disable global interrupts:
  if (currentClock == 0)                                               // Is VFO
    currentStep = (currentStep < lastStepVFO) ? (currentStep + 1) : 0; // Increment the step or go back to the first
  else                                                                 // Is BFO
    currentStep = (currentStep < lastStepBFO) ? (currentStep + 1) : 0;
  isFreqChanged = true;
  clearDisplay = true;
  elapsedTimeInterrupt = millis();
  interrupts(); // enable interrupts
}

// Change band
void changeBandUp()
{
  if (!digitalRead(BUTTON_BAND_DN)) {
    switchVFOBFO();
  }
  else {
    if ((millis() - elapsedTimeInterrupt) < MIN_ELAPSED_TIME)
      return;                                                       // nothing to do if the time less than 11 milisecounds
    noInterrupts();                                                 //  disable global interrupts:
    currentBand = (currentBand < lastBand) ? (currentBand + 1) : 0; // Is the last band? If so, go to the first band (AM). Else. Else, next band.
    vfoFreq = band[currentBand].minFreq;
    isFreqChanged = true;
    elapsedTimeInterrupt = millis();
    interrupts(); // enable interrupts
  }
}

void changeBandDn()
{
  if (!digitalRead(BUTTON_BAND_UP)) {
    switchVFOBFO();
  }
  else {
    if ((millis() - elapsedTimeInterrupt) < MIN_ELAPSED_TIME)
      return;                                                       // nothing to do if the time less than 11 milisecounds
    noInterrupts();                                                 //  disable global interrupts:
    currentBand = (currentBand > firstBand) ? (currentBand - 1) : lastBand; // Is the last band? If so, go to the first band (AM). Else. Else, next band.
    vfoFreq = band[currentBand].minFreq;
    isFreqChanged = true;
    elapsedTimeInterrupt = millis();
    interrupts(); // enable interrupts
  }
}
// Switch the Encoder control from VFO to BFO and virse versa.
void switchVFOBFO()
{
  if ((millis() - elapsedTimeInterrupt) < MIN_ELAPSED_TIME)
    return;       // nothing to do if the time less than 11 milisecounds
  noInterrupts(); //  disable global interrupts:
  currentClock = !currentClock;
  currentStep = 0; // go back to first Step (100Hz)
  clearDisplay = true;
  elapsedTimeInterrupt = millis();
  interrupts(); // enable interrupts
}

// main loop
void loop()
{
  // Enconder action can be processed after 5 milisecounds
  if ((millis() - elapsedTimeEncoder) > 5)
  {
    encoder_pin_a = digitalRead(ENCODER_PIN_A);
    encoder_pin_b = digitalRead(ENCODER_PIN_B);
    if ((!encoder_pin_a) && (encoder_prev)) // has ENCODER_PIN_A gone from high to low?
    {                                       // if so,  check ENCODER_PIN_B. It is high then clockwise (1) else counter-clockwise (-1)
      changeFreq(((encoder_pin_b) ? 1 : -1));
    }
    encoder_prev = encoder_pin_a;
    elapsedTimeEncoder = millis(); // keep elapsedTimeEncoder updated
  }
  // check if some action changed the frequency
  if (isFreqChanged)
  {
    if (currentClock == 0)
    {
      si5351.set_freq(vfoFreq, SI5351_CLK0);
    }
    else
    {
      si5351.set_freq(bfoFreq, SI5351_CLK1);
    }
    isFreqChanged = false;
    displayDial();
  }
  else if (clearDisplay)
  {
    display.clear();
    displayDial();
    clearDisplay = false;
  }
}
