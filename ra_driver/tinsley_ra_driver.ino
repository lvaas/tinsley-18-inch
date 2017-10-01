//  This program is for a Teensy3.2 microcontroller that drives the
//  replacement inverter circuit installed in 2017 in the Tinsley Labs
//  18" Cassegrain system in the Schlegel-McHugh observatory. Its
//  purpose is to generate the waveforms to drive the complementary
//  pull-down MOSFETS which drive he secondary winding of a 24V
//  center-tapped transformer, from the primary of which the 120V output
//  drives the RA motor.
//  
//  The circuit is designed to use the interface signal from the
//  existing controller, which is a 120HZ+/- pulse that originally
//  drove a capacitily-coupled flip-flop driver. The pulse rate is
//  controlled to provide the desired tracking rate (solar, sidereal,
//  etc.) as well as excursion of +/-25Hz for guiding.
//  
//  The driver circuit has a failsafe to prevent the drive transistors
//  staying turned on in case the processor freezes: the processor
//  outputs must be pulsed at about 2.5kHz in order to turn on the
//  transistors.
//  
//  The control input is a 2-pin optoisolated driver, basically an
//  LED with a series resistor designed for 5V drive.
//  
//  There is also a pushbutton on the board to activate test modes
//  to allow the motor to be driven independent of the control input.
//  One short press activates mode 1, where the motor is driven
//  constantly at sidereal rate. Two short presses activates mode 2,
//  which adds two 1-second excursions to the high and low guide
//  frequencies every 8 seconds. A long press resets to normal
//  operating mode.
//  
//  The LED on the Teensy anunciates the current operating condition
//  in a repeating pattern as follows:
//  - 1 short blink: mode 1
//  - 2 short blinks: mode 2
//  - short-long (or, always on with 2 short blinks off): normal, no drive
//  - 0.5s on, 0.5s off (50-50 slow): normal, driven at normal rate
//  - 0.1s on, 0.1s off (50-50 fast): normal, driven at fast rate
//  - longer blink with short off period: normal, driven at slow rate
//
// 09 June 2017 R. Hogg initial release

const int ledPin =  13;
const int drvAPin = 14;
const int drvBPin = 15;
const int ctrlPin = 23;
const int buttonPin = 22; // button to activate test modes

// use interrupt to catch narrow pulse on control input
unsigned long controlMicros = 0;
unsigned long prevControlMicros = 0;
void isrService() {
  unsigned long us = micros();
  controlMicros = us - prevControlMicros;
  prevControlMicros = us;
}

void setup() {
  pinMode(ledPin, OUTPUT);
  pinMode(drvAPin, OUTPUT);
  pinMode(drvBPin, OUTPUT);
  pinMode(ctrlPin, INPUT_PULLUP);
  pinMode(buttonPin, INPUT_PULLUP);
  attachInterrupt(ctrlPin, isrService, FALLING);
}

// millisecond-scale timing variables
unsigned long currentMillis = 0;
unsigned long prevMillis = 0;

// button state and debouncing variables
const int PRESSED = 0;
const int RELEASED = 1;
int buttonState = RELEASED;
int tmpButtonState = 0;
int buttonDebounceCount = 0;
int buttonDebounceThreshold = 10; // ms

// operating mode and variables
int opMode = 0;
const int rateOff = 0;
const int rateNormal = 1;
const int rateFast = 2;
const int rateSlow = 3;
int rate = rateNormal;

// LED control variables and functions
int ledState = LOW;
unsigned long ledNextMillis = 0;
int ledBlinkCount = 0;

void ledUpdate() { // called every 1ms
  int ledDelayMillis = 50; // should be overridden
  if (currentMillis < ledNextMillis) return;
  if (ledState == LOW) {
    ledState = HIGH;
    if (opMode == 0) {
      if (rate == rateOff) ledDelayMillis = ++ledBlinkCount % 2 ? 250 : 1000;
      if (rate == rateFast) ledDelayMillis = 250;
      if (rate == rateNormal) ledDelayMillis = 1000;
      if (rate == rateSlow) ledDelayMillis = 1200;
    }
    else ledDelayMillis = 100;
  } else {
    ledState = LOW;
    if (opMode == 0) {
      if (rate == rateOff) ledDelayMillis = 100;
      if (rate == rateFast) ledDelayMillis = 250;
      if (rate == rateNormal) ledDelayMillis = 1000;
      if (rate == rateSlow) ledDelayMillis = 50;
    } else {
      if (++ledBlinkCount >= opMode) {
        ledBlinkCount = 0;
        ledDelayMillis = 700;
      } else {
        ledDelayMillis = 100;
      }
    }
  }
  ledNextMillis = currentMillis + ledDelayMillis;
  digitalWrite(ledPin, ledState);
}

// operating mode control
int lastPressMillis = 0;
int lastReleaseMillis = 0;
int lastButtonEventMillis = 0;
int buttonEventPending = 0;
unsigned int pressDelayThreshold = 500;
int shortPressCount = 0;

void setNewOpMode(int mode) {
  opMode = mode;
  ledNextMillis = currentMillis;
  ledState = LOW;
  ledBlinkCount = 0;
}

void buttonChangedEvent() {
  if (buttonState == PRESSED) {
    if (currentMillis - lastReleaseMillis > pressDelayThreshold) {
      shortPressCount = 0;
    }
    buttonEventPending = 1;
    lastPressMillis = currentMillis;
    lastButtonEventMillis = currentMillis;
  } else {
    if (currentMillis - lastPressMillis <= pressDelayThreshold) {
      ++shortPressCount;
      buttonEventPending = 1;
    }
    lastReleaseMillis = currentMillis;
    lastButtonEventMillis = currentMillis;
  }
}

void buttonStableEvent() { // button stable for pressDelayThreshold ms
  if (buttonState == PRESSED) { // long press, reset
    setNewOpMode(0);
  } else {
    if (shortPressCount > 0) {
      setNewOpMode(shortPressCount);
      shortPressCount = 0;
    }
  }
  buttonEventPending = 0;
}

void buttonUpdate() { // called every 1ms
  int buttonSample = digitalRead(buttonPin);
  if (tmpButtonState != buttonSample) {
    tmpButtonState = buttonSample;
    buttonDebounceCount = 0;
  } else {
    if (buttonState != tmpButtonState) {
      if (++buttonDebounceCount >= buttonDebounceThreshold) {
        buttonState = tmpButtonState;
        buttonChangedEvent();
      }
    } else {
      if (buttonEventPending && (currentMillis - lastButtonEventMillis > pressDelayThreshold)) {
        buttonStableEvent();
      }
    }
  }
}

// inverter output driver
unsigned long currentMicros;
unsigned long cycleMicros;
unsigned long refMicros = 0;
unsigned long pulseHighMicros = 4800; // tuned in circuit simulator for best efficiency
const unsigned long mode1HalfPdMicros = 8311; // sidereal
const unsigned long mode2FastMicros = 6897; // + 25Hz
const unsigned long mode2SlowMicros = 10526; // - 25Hz
const unsigned long fastTresholdMicros = 7957; // for LED anunciation
const unsigned long slowTresholdMicros = 8864; // for LED anunciation
int pulsePhase = 0; // 0 = pin A, 1 = pin B
unsigned long halfPd = 0; // half-period when driving, 0 when off

void driverLoop() // called every time from main loop
{
  currentMicros = micros();
  cycleMicros = currentMicros - refMicros;

  if (halfPd == 0) {
    digitalWrite(drvAPin, LOW);
    digitalWrite(drvBPin, LOW);
  } else {
    if (cycleMicros > halfPd) {
      refMicros = currentMicros; // reset cycle
      cycleMicros = currentMicros - refMicros;
      pulsePhase = 1 - pulsePhase; // alternate between A and B transistors
    }
    if (cycleMicros <= pulseHighMicros) {
      if (pulsePhase == 0) {
        // the drive circuit contains a failsafe that turns off the
        // drivers if the processor freezes in any state; the output
        // must be pulsed at a 400-us period to keep the transistor on.
        digitalWrite(drvAPin, (cycleMicros % 400) >= 200 ? LOW : HIGH);
      } else {
        digitalWrite(drvBPin, (cycleMicros % 400) >= 200 ? LOW : HIGH);
      }
    } else {
      digitalWrite(drvAPin, LOW);
      digitalWrite(drvBPin, LOW);
    }
  }
}

void driverUpdate() {
  if (opMode == 1) {
    halfPd = mode1HalfPdMicros;
  } else if (opMode == 2) {
    int phase = (currentMillis / 1000) % 8;
    if (phase == 3) halfPd = mode2FastMicros;
    else if (phase == 7) halfPd = mode2SlowMicros;
    else halfPd = mode1HalfPdMicros;
  } else if (opMode != 0) {
    halfPd = 0;
  }
}

// control input detection
//
// In normal mode, we are controlled by a pulse that occurs once
// every half-period, mirroring how the original 1968 circuit
// worked. Here we are measuring that so it can be duplicated
// in the output driver. In addition we are adding some range
// checking, rejection of spurious pulses, and indication of the
// mode via the LED.
//
// The pulse is detected by the interrupt service routine at the
// beginning of this file.

void checkControl() {
  if ((micros() - prevControlMicros) > mode1HalfPdMicros * 8) { // turn off after allowing for some missed pulses
    if (opMode == 0) {
      halfPd = 0;
      rate = rateOff;
    }
    controlMicros = 0;
  } else if (controlMicros > 6233 && controlMicros < 10389 && opMode == 0) { // +/- 25% passband
    halfPd = controlMicros;
    // save rate category for LED anunciation
    if (halfPd < fastTresholdMicros)  rate = rateFast;
    else if (halfPd > slowTresholdMicros) rate = rateSlow;
    else rate = rateNormal;
  }
}

void millisecondLoop() {
  buttonUpdate();
  checkControl();
  ledUpdate();
  driverUpdate();
}

void loop() {
  currentMillis = millis();

  if (prevMillis < currentMillis) {
    prevMillis = currentMillis;
    millisecondLoop();
  }
  driverLoop();
}
