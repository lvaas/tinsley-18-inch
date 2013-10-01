#include <TimerOne.h>
#include <Wire.h>

const int I2C_ADDR = 42;
const int RA_DRIVE_PIN = 9;
const int LED_PIN = 13; // used for relay

const int relay_pin[] = {2, 4, 5, 6, 7, 8, 10, 11, 12, 13};
const int N_RELAYS = 10;
const int relay_nav_mask = 0x033; // N is 0x001 ... dome_right is 0x200
const int relay_north_mask = 0x001;
const int relay_south_mask = 0x002;
int prev_relays;
int relays;

const int st4_pin[] = {A0, A1, A2, A3};
const int st4_east = 0;
const int st4_north = 1;
const int st4_south = 2;
const int st4_west = 3;

const int RADuty = 10;
const int RADefaultRate = 8311; // sidereal
int curRARate, RARate, RAGuiding;
const int RAGuideMask = 16384;
const int RARateMask = RAGuideMask - 1;
const int RASolar = 8333;
const int RADeltaEast = 10526 - RASolar; // ST-4 Guiding, -25Hz
const int RADeltaWest = 6897 - RASolar; // ST-4 Guiding, +25Hz

const int N_I2C_BYTE = 32;

uint8_t i2c_command;
uint8_t i2c_data_in[N_I2C_BYTE];
uint8_t i2c_data_out[N_I2C_BYTE];
int i2c_bytes_out;

void setRA(int rate) {
    if(rate == 0) {
        Timer1.pwm(RA_DRIVE_PIN, 0, RADefaultRate); // off
    } else {
        Timer1.pwm(RA_DRIVE_PIN, RADuty, rate);
    }
}

void setup() {
    for(int idx = 0; idx < N_RELAYS; ++idx) {
        digitalWrite(relay_pin[idx], 0);
        pinMode(relay_pin[idx], OUTPUT);
    }
    prev_relays = 0;
    relays = 0;
    for(int idx = 0; idx < 4; ++idx) {
        pinMode(st4_pin[idx], INPUT_PULLUP);
    }
    curRARate = RADefaultRate;
    pinMode(RA_DRIVE_PIN, OUTPUT);
    Timer1.initialize(RADefaultRate);
    setRA(0); // off
	Wire.begin(I2C_ADDR);
	Wire.onReceive(ALAMODE_onReceive);
	Wire.onRequest(ALAMODE_onRequest);
    i2c_bytes_out = 0;
}

void ALAMODE_onReceive(int n_byte) {
    i2c_command = Wire.read();
    int idx = 0;
    while(Wire.available() && (idx < N_I2C_BYTE)){
        i2c_data_in[idx] = Wire.read();
        idx++;
    }
    if(i2c_command == 'F' && idx >= 2) {
        relays = i2c_data_in[0] << 8 | i2c_data_in[1];
    }
    if(i2c_command == 'R' && idx >= 2) {
        RARate = i2c_data_in[0] << 8 | i2c_data_in[1];
        RAGuiding = (RARate & RAGuideMask) != 0;
        RARate &= RARateMask;
    }
}

uint8_t data[5];

void ALAMODE_onRequest() {
    if(i2c_bytes_out < 1) { // not sure if this is an issue, just being defensive
        i2c_bytes_out = 1;
        i2c_data_out[0] = 255;
    }
	Wire.write(i2c_data_out, i2c_bytes_out);
}

void loop() {
    int RADelta = 0;
    int RTRelays = relays;
    if(((relays & relay_nav_mask) == 0) && RAGuiding == 0) {
        int st4_n = digitalRead(st4_pin[st4_north]);
        int st4_s = digitalRead(st4_pin[st4_south]);
        if(st4_n != st4_s) { // prohibit contention
            if(st4_n == 0) RTRelays |= relay_north_mask;
            if(st4_s == 0) RTRelays |= relay_south_mask;
        }
        int st4_e = digitalRead(st4_pin[st4_east]);
        int st4_w = digitalRead(st4_pin[st4_west]);
        if(st4_e != st4_w) { // prohibit contention
            if(st4_e == 0) RADelta = RADeltaEast;
            if(st4_w == 0) RADelta = RADeltaWest;
        }
    }
    int delta = RTRelays ^ prev_relays;
    if(delta) {
        for(int mask = 1, idx = 0; idx < N_RELAYS; mask <<= 1, ++idx) {
            if(delta & mask) {
                digitalWrite(relay_pin[idx], RTRelays & mask ? 1 : 0);
            }
        }
        prev_relays = RTRelays;
    }
    int RA_RTRate = RARate;
    if(RA_RTRate != 0) RA_RTRate += RADelta;
    if(curRARate != RA_RTRate) {
        curRARate = RA_RTRate;
        setRA(curRARate);
    }
}