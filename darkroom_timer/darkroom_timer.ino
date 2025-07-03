
// Rewritten for the TM1638 without rotary encoder by Gavin Lyons.
// Based on Monkito darkroom f-stop timer software written and © by Elia Cottier//
#include <TM1638plus.h>
#include <math.h> //Math library for "round" function
#include <EEPROM.h> //EEPROM library to save set-up values

// GPIO I/O pins on the Arduino or ESP 12 Relay
//pick on any I/O you want.
#if defined(__AVR__)
// Arduino
  #define  STROBE_TM 10
  #define  CLOCK_TM 9
  #define  DIO_TM 11
  #define TONE_PIN 12 //buzzer pin
#elif defined(ESP8266)
// ESP8266 specific code here
  #define  STROBE_TM 14
  #define  CLOCK_TM 13
  #define  DIO_TM 12
  #define TONE_PIN 15 //buzzer pin
#endif

bool high_freq = false; //default false Arduino Uno,, If using a high freq CPU > ~100 MHZ set to true, i.e ESP32
bool focusLight=false;
bool stripTestMode=false;
bool baseExposure=false;

#define RELAY_PIN 5 //relay board pin
#define FOCUS_LED_PIN 0 //Focus button led pin
#define START_LED_PIN 7 //Start button led pin
#define CORR_LED_PIN 6 //Start button led pin

//TM1638 Single Buttons
#define CANCEL_BUTTON 1 //S1
#define FOCUS_BUTTON 2 //S2 
#define STRIPTEST_BUTTON 4 //S3
#define BRIGHTNESS_BUTTON 8 //S4 
#define INCREMENT_BUTTON 16 //S5
#define MINUS_BUTTON 32 //S6
#define PLUS_BUTTON 64 //S7
#define START_BUTTON 128 //S8

//TM1638 Buttons Combos
#define SNAP_TO_SCALE_BUTTON 96 //S6 + S7
#define SHIFT_MINUS_BUTTON 33 //S1 + S6
#define SHIFT_PLUS_BUTTON 65 //S1 + S7
#define SCALE_SETUP_BUTTON 65 //S1 + S7
#define CANCEL_SCALE_BUTTON 129 //S1 + S8
#define RESET_TO_ZERO_BUTTON 22 //S2 + S3 + S5

//TM1638 Buttons - old definitions
#define STRIPTEST_MODE_BUTTON 5 //S1 + S3 - not used?

//EPROM default valuse storage
  const byte eeBrightness = 0; //eeprom brightness value address
  const byte eeIncrement = 1; //eeprom f-stop selector increment value address
  const byte eeLastFStopValue = 3; //eeprom step program input mode value address
  const byte eeStepIdx = 6; //eeprom step program input mode value address

  byte brightnessValue; 
  uint8_t tmButtons;
  const long intervalButton = 300; // interval to read button (milliseconds)
  byte debounce(10); //general debounce delay, ok for buttons and encoder
  byte uiMode; //mode for main loop mode management
  int displayRefreshTracker; //track encoder change used by all selection functions
  byte bip(100); //bip duration
  int shortTone (500); //short tone duration
  int longTone (1000); //long tone duration
  int toneLow(880); //lower frequency tone (A 4th)
  int toneHigh(1780); //higher frequency tone (A 5th)
  byte shortPause(150); //short tone pause
  int readingDelay = 1000;//1 sec. human reading delay
  char tempString[9]; //TM1638 Display digits with two decimals.

  volatile unsigned int buttonPlusMinusValue; //+/- exposure functions
  int increment; 
  int plusminus; // plusminus button value
  bool loadDefault;
  
//Timer
  byte timerIncrement []= {8,16,33,50,100}; //Preset f-stop fractions increments in hundreds
  byte timerIncrementSize = 5;
  byte timerInc; //fstop increment value

  unsigned int FStop; //F-stop value
  unsigned int tensSeconds; //time value in 1/10th of seconds
  unsigned int resumeTime; //Resume time value
  double deltaFStop; //scaling f-stop delta value
  double lengthRatio = 0;
  unsigned long timeMillis;//time in millis
  unsigned long startMillis;//start time
  unsigned long elapsedMillis;//elapsed time
  unsigned long millisInterval = 100;//10 Hz timer display update frequency
  unsigned long time_passed;//elapsed button time 
  int stepIdx;

//Constructor object (GPIO STB , GPIO CLOCK , GPIO DIO, use high freq MCU)
TM1638plus tm(STROBE_TM, CLOCK_TM , DIO_TM, high_freq);
void setup()
{
  Serialinit();
  tm.displayBegin();
//EEPROM
  #if defined(ESP8266) 
  EEPROM.begin(6);// Allocate The Memory Size Needed
  #endif
  timerInc = EEPROM.read(eeIncrement);//f-stop buttonPlueMinus increment
  stepIdx = EEPROM.read(eeStepIdx);
  if (stepIdx==0) stepIdx=3;
  if (timerInc==0) timerInc=timerIncrement[2];
  tm.setLED(stepIdx, 1);
  brightnessValue = EEPROM.read(eeBrightness);
//Values
  FStop = 0;
  deltaFStop = 0;
  timeMillis = 0;
  elapsedMillis = 0;
  buttonPlusMinusValue = 0;
  plusminus=0;
  increment = 0;
  displayRefreshTracker = 10000; //value for displays refresh
  time_passed = millis();
  loadDefault=true;
 //Pin modes
  pinMode(TONE_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT); 
//Display
  brightnessInit();
  bipHigh();//set-up end signal
}

void loop()
{
  uiModes();
}

void uiModes() //timer mode and related functions
{
  tmButtons = buttonsRead(); //check buttons events
  if (tmButtons != 0) displayRefreshTracker = 10000;//Value to get any display update
 
  if (uiMode==0)
  {
    plusminus=0;
    switch(tmButtons)
    {
      case RESET_TO_ZERO_BUTTON: //Buttons 2+3+5 resets to f0
        uiMode = 1; //Clear buttonPlueMinus before timer mode (uiMode 0)
      break;
      case STRIPTEST_BUTTON: //S3
         uiMode = 2; //Strip Test Mode
      break;
      case FOCUS_BUTTON: //S2
        uiMode = 4; //Focus light on/off
      break;
      case BRIGHTNESS_BUTTON: //S4
        uiMode = 99; //Brightness set-up
        break;
      case INCREMENT_BUTTON://F-stop increment set-up - S5
        uiMode = 14; 
      break;
      case STRIPTEST_MODE_BUTTON: //- not used?:
        uiMode=16;
      break;
      case SCALE_SETUP_BUTTON: //S1 + S7
        uiMode = 18; //Scaling set-up
      break;
      case CANCEL_SCALE_BUTTON: //S1 + S8
        uiMode = 19; //Scaling reset
      break;
      case SNAP_TO_SCALE_BUTTON: //S6 + S7
        uiMode = 20; // Snap to Stop mode
      break;
    }
  }
  else
  {
    if(millis()-time_passed > 5000) {
      time_passed = millis(); 
      uiMode=0;
    }
  }
 
  switch (uiMode)
    {
    case 1: //resets to f0 when you hold buttons 2+3+5
      buttonPlusMinusValue = 0;
      uiMode = 0; //default mode
    break;
    case 2: //starts a strip test
      stripTest();
    break;
    case 3: //not used?
      uiMode = 22;
    break;
    case 4:
      focusOnOff(); // Focus Lamp on/off
    break;
    case 12: //not used?
      focusOnOff();
    break;
    case 14: //set increments
      fstopIncrementSetUp();
    break;
    case 18: //set up scaling
      scaleCalculator();
    break;
    case 19: //exit scaling
      clearCorrection();
    break;
    case 99: //adjust brightness
      brightnessSelector();
    break;
    case 20: // Snap to nearest Stop
      snapToNearestStop();
    break;
    default:
     fstopSelector(); //default mode
    } 
}
// Display Text with decimal points
void displayText(const char *text, int p, int p2) {
  char c, pos=0;
  while ((c = (*text++)) && pos < TM_DISPLAY_SIZE)  {
    if ((p==pos ||p2==pos) && p>0) {
      tm.displayASCIIwDot(pos++, c);
    }  else {
      tm.displayASCII(pos++, c);
    }
  }
}
// Reset LED on the TM1638
void clearStripLEDs() {
  for (uint8_t LEDposition = 1; LEDposition < 7; LEDposition++) {
    tm.setLED(LEDposition, 0);
  }
}

// Read and debounce the buttons from TM1638  every interval 
uint8_t buttonsRead(void)
{
  uint8_t buttons = 0;
  static unsigned long previousMillisButton = 0;  // executed once 
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisButton >= intervalButton) {
    previousMillisButton = currentMillis;
    buttons = tm.readButtons();
  }
  return buttons;
}

//Function to setup serial called from setup FOR debug
void Serialinit()
{
  Serial.begin(9600);
  Serial.println("F-STOP Timer");
}
