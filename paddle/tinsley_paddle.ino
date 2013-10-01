/* LVAAS Tinsley 18" Cassegrain paddle code (Teensy3 processor)
*/

/* #define DEBUG_CODE */

unsigned long loopMillis; /* value of millis() upon entering loop() */
int ledDelay = 1000; /* half-period of LED blink */

/* ILLUMINATION LED */

const int lightPin = 9;

unsigned lightLevel[4] = {0, 4096, 65535, 0}; /* off, low, high, sentinel */
int currentLightLevel = 3;

int dimmerEasterEggActive = 0;

void setLightLevel(int level) {
    static int prevLevel = -1;
    currentLightLevel = level;
    int newLevel = lightLevel[level];
    if(prevLevel != newLevel) {
        prevLevel = newLevel;
        analogWrite(lightPin, newLevel);
    }
}

/* VAR RA KNOB INPUT */

const int potPin = A0;

int raGetSetting() {
    const int deadbandWidth = 6; /* during operation, allow for this much jitter without reporting */
    static int deadbandBottom = -1;
    static int deadbandTop = -1;
    int setting = analogRead(potPin);
    int report = -1; /* if still in deadband */
    if(deadbandTop < setting) {
        deadbandTop = setting;
        if(deadbandBottom < deadbandTop - deadbandWidth) {
            deadbandBottom = deadbandTop - deadbandWidth;
            report = deadbandBottom;
        }
    }
    if(deadbandBottom > setting) {
        deadbandBottom = setting;
        report = deadbandBottom;
        if(deadbandTop > deadbandBottom + deadbandWidth) {
            deadbandTop = deadbandBottom + deadbandWidth;
        }
    }
    if(report != -1 && dimmerEasterEggActive == 1) {
        lightLevel[currentLightLevel] = 64 * report;
        setLightLevel(currentLightLevel);
    }
    return report;
}

const int raMsgSize = 8; /* one extra, only need 6 + terminator */
int raMsg[raMsgSize] = {0, 0, 0, 0, 0, 0, 0, 0};
#ifdef DEBUG_CODE
int raMsgASCII[raMsgSize];
#endif
int raMsgIndex = raMsgSize - 1; /* point to terminator, no message */
const int kbDigitKeycodes[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};

void raUpdate() { /* if previous message sent, sample and create new one if needed */
    if(raMsg[raMsgIndex] != 0) return; /* wait for previous message to be dispatched */
    int value = raGetSetting();
    if(value == -1) return; /* no update */
    int index = 0;
#ifdef DEBUG_CODE
    raMsgASCII[index] = 'V';
#endif
    raMsg[index++] = KEY_V;
    int radix = 1000;
    while(radix > 0) {
        int digit = (value / radix);
#ifdef DEBUG_CODE
        raMsgASCII[index] = '0' + digit;
#endif
        raMsg[index++] = kbDigitKeycodes[digit];
        value -= digit * radix;
        radix /= 10;
    }
#ifdef DEBUG_CODE
    raMsgASCII[index] = '.';
#endif
    raMsg[index++] = KEY_ENTER;
#ifdef DEBUG_CODE
    raMsgASCII[index] = 0;
#endif
    raMsg[index++] = 0;
    raMsgIndex = 0; /* send message */
}

/* RAW KEYBOARD */

const int kbNumRows = 4;
const int kbScanRowPins[kbNumRows] = {0, 1, 2, 3};
const int kbNumColumns = 4;
const int kbScanColumnPins[kbNumColumns] = {4, 5, 6, 7};

long kbRawState() { /* scan keyboard hardware and return result */
    long kb = 0;
    long mask = 1;
    for(int row = 0; row < kbNumRows; ++row) {
        digitalWrite(kbScanRowPins[row], 0); /* enable row to pull down column inputs through keys/diodes */
        for(int column = 0; column < kbNumColumns; ++column) {
            if(digitalRead(kbScanColumnPins[column]) == 0) { /* key is closed, pulling down input */
                kb |= mask;
            }
            mask <<= 1;
        }
        digitalWrite(kbScanRowPins[row], 1);
    }
    return kb;
}

/* DEBOUNCE KEYBOARD */

const int kbRawNumKeys = kbNumRows * kbNumColumns;

long kbDebounce(long raw) {
    const int debounce_ms = 30; /* key transitions are not recognized less than this interval after last (recognized) transition */
    static unsigned long prevTrans[kbRawNumKeys] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static long debounced = 0;
    
    long delta = raw ^ debounced;
    long mask = 1;
    for(int key = 0; key < kbRawNumKeys; ++key) {
        if((delta & mask) != 0) {
            if(prevTrans[key] + debounce_ms < loopMillis || prevTrans[key] > loopMillis) {
                prevTrans[key] = loopMillis;
                debounced = (debounced & ~mask) | (raw & mask);
            }
        }
        mask <<= 1;
    }
    return debounced;
}

/* KEYBOARD DEFINITION */

const int kbKeys = kbRawNumKeys + 6; /* add six "virtual" keys to represent center-off switch settings and simultaneous-press easter eggs */
int kbASCIICodes[kbKeys];
int kbKeyCodes[kbKeys];
int kbMomentary[kbKeys];
int kbLightSetting[kbKeys];

const int kbInteractions = 7; /* number of key interactions defined */
long kbExcludeMasks[kbInteractions]; /* 2-bit masks for mutually excluded key pairs */
long kbNegMasks[kbInteractions]; /* corresponding 1-bit masks for virtual keys active if both real keys inactive */
long kbEggMasks[kbInteractions]; /* corresponding 1-bit masks for virtual "easter-egg" keys to send if both keys pressed */

long dimmerEasterEggMask;

int kbDefineKey(int ascii, int keyCode, int momentary, int lightSetting) {
    static int index = 0;
    
    kbASCIICodes[index] = ascii;
    kbKeyCodes[index] = keyCode;
    kbMomentary[index] = momentary;
    kbLightSetting[index] = lightSetting;
    return index++;
}

void kbDefineInteraction(int key1, int key2, int keyNeg, int keyEasterEgg) {
    static int index = 0;
    
    kbExcludeMasks[index] = (1l << key1) | (1l << key2);
    if(keyNeg == -1) {
        kbNegMasks[index] = 0l;
    } else {
        kbNegMasks[index] = 1l << keyNeg;
    }
    if(keyEasterEgg == -1) {
        kbEggMasks[index] = 0l;
    } else {
        kbEggMasks[index] = 1l << keyEasterEgg;
    }
    ++index;
}

void kbDefineKeyboard() {
    /* MUST CALL kbDefineKey() exactly kbKeys times!!! */
    int kbNorth    = kbDefineKey('N', KEY_N, 1, -1);
    int kbSouth    = kbDefineKey('S', KEY_S, 1, -1);
    int kbEast     = kbDefineKey('E', KEY_E, 1, -1);
    int kbWest     = kbDefineKey('W', KEY_W, 1, -1);
    int kbIn       = kbDefineKey('I', KEY_I, 1, -1); /* Sec Focus In */
    int kbOut      = kbDefineKey('O', KEY_O, 1, -1); /* Sec Focus Out */
    int kbLeft     = kbDefineKey('L', KEY_L, 1, -1); /* Dome Left */
    int kbRight    = kbDefineKey('R', KEY_R, 1, -1); /* Dome Right */
    int kbDecNorm  = kbDefineKey('F', KEY_F, 0, -1); /* Declination Normal */
    int kbDecRev   = kbDefineKey('B', KEY_B, 0, -1); /* Declineation Reversed */
    int kbLightHi  = kbDefineKey('H', KEY_H, 0,  2); /* Paddle light High (switch forward) */
    int kbLightOff = kbDefineKey('Z', KEY_Z, 0,  0); /* Paddle light off (switch back) */
    int kbSet      = kbDefineKey('T', KEY_T, 0, -1); /* Set (fast paddle navigation) */
    int kbGuide    = kbDefineKey('G', KEY_G, 0, -1); /* Guide (slow paddle navigation) */
    int kbUnused1  = kbDefineKey(  0,     0, 0, -1); /* not present */
    int kbUnused2  = kbDefineKey(  0,     0, 0, -1); /* not present */
    int kbLightLo  = kbDefineKey('D', KEY_D, 0,  1); /* VIRTUAL KEY - light turned low (switch centered) */
    int kbNavOff   = kbDefineKey('X', KEY_X, 0, -1); /* VIRTUAL KEY - navigation switch centered */
    int kbNSEgg    = kbDefineKey('U', KEY_U, 1, -1); /* VIRTUAL KEY - N-S simultaneous-press easter egg */
    int kbEWEgg    = kbDefineKey('J', KEY_J, 1, -1); /* VIRTUAL KEY - E-W simultaneous-press easter egg */
    int kbIOEgg    = kbDefineKey('K', KEY_K, 1, -1); /* VIRTUAL KEY - I-O simultaneous-press easter egg */
    int kbLREgg    = kbDefineKey('M', KEY_M, 1, -1); /* VIRTUAL KEY - L-R simultaneous-press easter egg */
    
    /* MUST CALL kbDefineInteraction() exactly kbInteractions times!!! */
    kbDefineInteraction(kbNorth, kbSouth, -1, kbNSEgg);
    kbDefineInteraction(kbEast, kbWest, -1, kbEWEgg);
    kbDefineInteraction(kbIn, kbOut, -1, kbIOEgg);
    kbDefineInteraction(kbLeft, kbRight, -1, kbLREgg);
    kbDefineInteraction(kbDecNorm, kbDecRev, -1, -1);
    kbDefineInteraction(kbLightHi, kbLightOff, kbLightLo, -1);
    kbDefineInteraction(kbSet, kbGuide, kbNavOff, -1);
    
    dimmerEasterEggMask = (1l<<kbSouth) | (1l<<kbWest) | (1l<<kbNavOff);
}

/* KEYBOARD INTERACTIONS */

long kbResolveInteractions(long kb) {
    for(int i = 0; i < kbInteractions; ++i) {
        if((kb & kbExcludeMasks[i]) == kbExcludeMasks[i]) {
            kb &= ~kbExcludeMasks[i];
            kb |= kbEggMasks[i];
        }
        if(kbNegMasks[i] != -1) {
            kb = (kb & ~kbNegMasks[i]) | (((kb & kbExcludeMasks[i]) == 0) ? kbNegMasks[i] : 0);
        }
    }
    return kb;
}

/* KEYBOARD PACING */

/* pace momentary keys one at a time with repeats at intervals, to avoid using too many slots in USB packet */
/* also inject RA message, if present */

int kbMessageCode; /* value for injected message byte */
#ifdef DEBUG_CODE
int kbMessageASCII;
#endif

long kbPace(long kb) {
    long paced_kb = 0;
    long mask = 1l;
    static unsigned long lastMsgReport = 0;
    static unsigned long lastReport[kbKeys] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const int reportInterval = 5000;
    const int offset = reportInterval + 100;
    const int reportDurationDown = 12;
    const int reportDurationUp = 12;
    const int maxSlots = 2;
    int slotsUsed = 0;
    unsigned long offsetMillis = loopMillis + offset;
    
    dimmerEasterEggActive = ((kb & dimmerEasterEggMask) == dimmerEasterEggMask) ? 1 : 0;

    if((lastMsgReport <= offsetMillis) && (offsetMillis <= (lastMsgReport + reportDurationDown))) { /* prev message byte still "down" */
        paced_kb |= 1 << kbKeys; /* extra bit for message byte */
        ++slotsUsed;
    } else if(offsetMillis > (lastMsgReport + 2 * reportDurationUp) /* allow for space between bytes */
            && raMsg[raMsgIndex] != 0) { /* inject new message byte */
        lastMsgReport = offsetMillis;
#ifdef DEBUG_CODE
        kbMessageASCII = raMsgASCII[raMsgIndex];
#endif
        kbMessageCode = raMsg[raMsgIndex++];
        paced_kb |= 1 << kbKeys; /* extra bit for message byte */
        ++slotsUsed;
    }
    for(int key = 0; key < kbKeys; ++key) {
        if((kb & mask) != 0) {
            if(kbMomentary[key]) {
                paced_kb |= mask;
            } else {
                if(slotsUsed < maxSlots) {
                    if(lastReport[key] > offsetMillis) { /* loopMillis wrap-around - will hang for a few seconds - 1200 hours after boot */
                        lastReport[key] = 0;
                    }
                    if((lastReport[key] <= offsetMillis) && (offsetMillis <= (lastReport[key] + reportDurationDown))) { /* key still down */
                        paced_kb |= mask;
                        ++slotsUsed;
                    } else if((lastReport[key] + reportInterval) < offsetMillis) { /* time for another report */
                        lastReport[key] = offsetMillis;
                        paced_kb |= mask;
                        ++slotsUsed;
                    }
                }
            }
            if(kbLightSetting[key] != -1) {
                setLightLevel(kbLightSetting[key]);
            }
        } else {
            lastReport[key] = 0;
        }
        mask <<= 1;
    }
    return paced_kb;
}

/* KEYBOARD REPORTING */

/* report keyboard state to USB host */

#ifdef DEBUG_CODE
/* DEBUG code: report keyboard state via Serial until simultaneous N+E */

int kbReportMode = 0;
#endif

/* Apparently the library does not work right if you switch one slot from one
pressed key to another without sending once with the release event.  So,
it is necessary to track what each slot is used for to prevent this. */

const int reportPacing = 4; /* min ms between reports */

/* USB KEYBOARD HELPERS */

const int usbMax = 6; /* maximum number of simultaneous keys */
int usbKeyValue[usbMax];
int usbReserved[usbMax] = {-1, -1, -1, -1, -1, -1};
int usbRelease[usbMax]; /* flag to release at end of loop */
int usbSlot[kbKeys + 1] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

void kbReport(long kb) {
    static long kbReported = 0;
    static long lastReport = -1;

    if(kbReported != kb) {
#ifdef DEBUG_CODE
        kbReported = kb;
        if(kbReportMode == 0) {
            if((kb & 5) == 5) {
                kbReportMode = 1;
                ledDelay = 500;
            } else {
                long mask = 1l;
                for(int key = 0; key < kbKeys; ++key) {
                    if(kb & mask) Serial.print((char)kbASCIICodes[key]);
                    else Serial.print('-');
                    mask <<= 1;
                }
                if(kb & mask) Serial.print((char)kbMessageASCII); /* message injection */
                Serial.println("");
                return;
            }
        }
#endif
        if(loopMillis < lastReport + reportPacing) return;
        lastReport = loopMillis;
        long mask = 1l;
        int slot;
        for(slot = 0; slot < usbMax; ++slot) usbRelease[slot] = 0;
        long delta = kb ^ kbReported;
        for(int key = 0; key < kbKeys + 1; ++key) {
            if(delta & mask) {
                if(kb & mask) {
                    for(slot = 0; slot < usbMax; ++slot) {
                        if(usbReserved[slot] == -1) {
                            usbReserved[slot] = key;
                            usbSlot[key] = slot;
                            if(key == kbKeys) {
                                usbKeyValue[slot] = kbMessageCode; /* message injection */
                            } else {
                                usbKeyValue[slot] = kbKeyCodes[key];
                            }
                            kbReported |= mask;
                            break;
                        }
                    }
                } else {
                    slot = usbSlot[key];
                    usbKeyValue[slot] = 0;
                    usbRelease[slot] = 1;
                    kbReported &= ~mask;
                }
            }
            mask <<= 1;
        }
        for(slot = 0; slot < usbMax; ++slot) {
            if(usbRelease[slot]) {
                int k = usbReserved[slot];
                usbReserved[slot] = -1;
                usbSlot[k] = -1;
            }
        }
        Keyboard.set_modifier(0);
        Keyboard.set_key1(usbKeyValue[0]);
        Keyboard.set_key2(usbKeyValue[1]);
        Keyboard.set_key3(usbKeyValue[2]);
        Keyboard.set_key4(usbKeyValue[3]);
        Keyboard.set_key5(usbKeyValue[4]);
        Keyboard.set_key6(usbKeyValue[5]);
        Keyboard.send_now();
    }
}


const int ledPin = 13;

void setup() {
    for(int row = 0; row < kbNumRows; ++row) {
        pinMode(kbScanRowPins[row], OUTPUT);
        digitalWrite(kbScanRowPins[row], 1);
    }
    for(int column = 0; column < kbNumColumns; ++column) {
        pinMode(kbScanColumnPins[column], INPUT_PULLUP);
    }
    analogWriteFrequency(lightPin, 366); /* as per http://www.pjrc.com/teensy/td_pulse.html */
    analogWriteResolution(16);
    setLightLevel(0); /* off */
    digitalWrite(lightPin, 0);
    pinMode(ledPin, OUTPUT);
    kbDefineKeyboard();
#ifdef DEBUG_CODE
    Serial.begin(115200);
#endif
}

int led = 0;
long prevLed = 0;

void loop() {
    loopMillis = millis();
    raUpdate();
    long kb = kbRawState();
    kb = kbDebounce(kb);
    kb = kbResolveInteractions(kb);
    kb = kbPace(kb);
    kbReport(kb);
    if(prevLed + ledDelay < loopMillis) {
        prevLed = loopMillis;
        led = 1 - led;
        digitalWrite(ledPin, led);
    }
    
    /*
    digitalWrite(lightPin, lightOn);
    */
}
