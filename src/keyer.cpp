// ESP8266 CW Networked Keyer

// Based on the following code:
// Morse Code Keyer (C) 2017 Doug Hoyte
// https://hoytech.com/articles/morse-code-keyer
// https://github.com/hoytech/morse-code-keyer
// 2-Clause BSD License
// Modified by ea4aoj 23/04/2020
// Modified by ea4hew 15/09/2020
// https://www.eacwspain.es/2021/05/22/morse-code-keyer-esp8266/

// Derivative work Copyright (C) 2022 Tim Sweeney K1BR
// Released under 2-Clause BSD License


// Basic Functions:
// Press the Setup button, enter the speed configuration mode, change the speed with the paddles.
// WPM will be announced, and you can interrupt with another key press. To exit press the Setup button again.
// LONG press Setup button, enters tone configuration mode, change tone with the paddles, to exit press the Setup button again.

// Long press on one of the memories to record memory, press Setup button when finished and it is memorized.
// Short press on one of the memories to play that memory.

// Press the Setup button and hold while immediately pressing a memory button:
// 1: Switch to paddle handler by pressing Memory1.
// 2: Switch to staright key by pressing Memory2.
// 3: Switch to vibroplex by pressing Memory3.

// Notes on networking:
// There is a 2 char delay on the server side to allow buffering. Inter-character timimg is preserved.
// Networking only functions in iambic mode.
// If you try to send a string of elements longer than 8, a packet will be sent, causing a slight pause in the sidetone.

// 2022-05-22 - Translate comments and configure for Platformio. Add inital Iambic Mode B code.
// 2022-05-23 - Move memory switches to A0.
// 2022-05-24 - Fixed dot completion.
// 2022-05-25 - Create processPaddles(), use it for both main loop and memory recording.
// 2022-05-26 - Add CW player, network init code (TODO: get network code working.)
// 2022-06-07 - Finalize basic network code, add ring buffer, tighten timimgs.
// 2022-06-12 - Fix memory and network playback timings.
// 2022-06-14 - Add EEPROM-rotate library, fix paddle debounce, add speed annoucements.
// 2022-06-16 - Update eeprom rotation reserved memory.


#include <Arduino.h>
#include <EEPROM_Rotate.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <CircularBuffer.h>

#define DEBUG_PIN
// #define DEBUG

#include <Pinflip.h>
#include <Debug.h>

#define SPKR 0
#define TX 1
#define NO_REC 0
#define REC 1
#define MORSE_NONE 0x01


// Morse encoding derived from KB8OJH
// https://kb8ojh.net/msp430/
const unsigned char morse_ascii[] = {
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  0x73, MORSE_NONE, 0x55, 0x32,                   /* , _ . / */
  0x3F, 0x2F, 0x27, 0x23,                         /* 0 1 2 3 */
  0x21, 0x20, 0x30, 0x38,                         /* 4 5 6 7 */
  0x3C, 0x3E, MORSE_NONE, MORSE_NONE,             /* 8 9 _ _ */
  MORSE_NONE, 0x31, MORSE_NONE, 0x4C,             /* _ = _ ? */
  MORSE_NONE, 0x05, 0x18, 0x1A,                   /* _ A B C */
  0x0C, 0x02, 0x12, 0x0E,                         /* D E F G */
  0x10, 0x04, 0x17, 0x0D,                         /* H I J K */
  0x14, 0x07, 0x06, 0x0F,                         /* L M N O */
  0x16, 0x1D, 0x0A, 0x08,                         /* P Q R S */
  0x03, 0x09, 0x11, 0x0B,                         /* T U V W */
  0x19, 0x1B, 0x1C, MORSE_NONE,                   /* X Y Z _ */
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
  MORSE_NONE, 0x05, 0x18, 0x1A,                   /* _ A B C */
  0x0C, 0x02, 0x12, 0x0E,                         /* D E F G */
  0x10, 0x04, 0x17, 0x0D,                         /* H I J K */
  0x14, 0x07, 0x06, 0x0F,                         /* L M N O */
  0x16, 0x1D, 0x0A, 0x08,                         /* P Q R S */
  0x03, 0x09, 0x11, 0x0B,                         /* T U V W */
  0x19, 0x1B, 0x1C, MORSE_NONE,                   /* X Y Z _ */
  MORSE_NONE, MORSE_NONE, MORSE_NONE, MORSE_NONE,
};


// PINS

const int pinSetup = D7;               // Press Setup (Adjust speed and tone)
const int pinKeyDit = D5;              // Key, dit paddle
const int pinKeyDah = D6;              // Key, dah paddle
const int pinStatusLed = D4;           // Led ESP8266 builin
const int pinMosfet = D0;              // Key rig jack
const int pinSpeaker = D8;             // Speaker


// STATE

const int stateIdle = 0;
const int stateSettingSpeed = 1;
const int stateSettingTone = 2;


// MODE TYPES

const int keyerModeIambic = 0;
const int keyerModeVibroplex = 1;
const int keyerModeStraight = 2;

const int netDisconnected = 0;
const int netClient = 1;
const int netServer = 2;


// SYMBOLS

const int symDit = 1;
const int symDah = 2;


// SAVE PACKET TYPES

const int packetTypeEnd = 0;
const int packetTypeSpeed = 1;
const int packetTypeFreq = 2;
const int packetTypeKeyerModeIambic = 3;
const int packetTypeKeyerModeVibroplex = 4;
const int packetTypeKeyerModeStraight = 5;
const int packetTypeMem0 = 20;
const int packetTypeMem1 = 21;
const int packetTypeMem2 = 22;


// UDP PACKET TYPES

const int udpFrame = 0;
const int udpKeepAlive = 2;
const int udpAck = 3;


// INTERNAL MEMORIES

const int storageSize = 2048;
const int storageMagic1 = 182;
const int storageMagic2 = 97;


// CONFIG DEFAULTS

int toneFreq = 700;                     // Default sidetone frequncy
unsigned int ditMillis = 60;            // Default speed (20 WPM)
int currKeyerMode = keyerModeIambic;    // Default mode
int iambicModeB = 1;                    // Default iambic mode

char memory[3][600];
size_t memorySize[3];

EEPROM_Rotate EEPROMr;

// See the Network.h file in the include subdirectory to configure the network.
#include <Network.h>

WiFiUDP udp;
struct DataPacket {
  unsigned int number;
  unsigned int data;
};

CircularBuffer < DataPacket, 10> packets;


// RUN STATE

int currState = stateIdle;
int prevSymbol = 0;                       // 0=none, 1=dit, 2=dah
int recording = 0;                        // Recording a memory
int currStorageOffset = 3;                // Base offset for the EEPROM memory block is 3
int playAlternate = 0;                    // Mode B completion flag
int ditDetected = 0;                      // Dit paddle hit during Dah play
int memSwitch = 0;                        // Memory switch set by readAnalog()
int netMode = netDisconnected;
unsigned long lastPacketSentTime = 0;     // in milli time
unsigned long keepAliveTimer = 0;         // in millis
unsigned long lastSymPlayedTime = 0;      // in milli time
unsigned long gap = 0;                    // gap from last packet sent to start of next char in millis
uint16_t packetCount = 0;
unsigned int toSend = 0;                  // stage to assemble the data portion of packet
uint16_t toChar = 0;                      // holds in bit pattern to be sent
uint16_t toLength = 0;                    // number of elements to  the character
int lastPacketType = 0;                   // what was last sent
int playNextPacket = 0;                   // buffer flag


DataPacket packet;


// FORWARD DECLARATIONS

void dumpSettingsToStorage();
void processPaddles(int ditPressed, int dahPressed, int transmit, int memoryId);
void memRecord(int memoryId, int value);
void sendPacket(unsigned int sendData, unsigned long spacing);


// LOW LEVEL FUNCTIONS

// Read the analog pin and assign a value to
// global memSwitch.
int readAnalog() {
  int value = analogRead(PIN_A0);
  if (value < 100) { return 0; }
  else if (value > 400 && value < 600) { return 1; }
  else if (value > 600 && value < 900) { return 2; }
  else if (value > 900) { return 3; } 
  return(0);
}


void playStraightKey(int releasePin) {
  tone(pinSpeaker, toneFreq);
  digitalWrite(pinStatusLed, HIGH);
  digitalWrite(pinMosfet, HIGH);

  while (digitalRead(releasePin) == LOW) {}
  
  noTone(pinSpeaker);
  digitalWrite(pinStatusLed, LOW);
  digitalWrite(pinMosfet, LOW);  
}


// EEPROMr FUNCTIONS

// Mark the end of a memory.
void saveStorageEmptyPacket(int type) {
  if (currStorageOffset + 1 >= storageSize) {
    dumpSettingsToStorage();
    return;
  }

  EEPROMr.write(currStorageOffset++, type);
  EEPROMr.write(currStorageOffset, packetTypeEnd);
  EEPROMr.commit();
}


// Store an int in memory.
void saveStorageInt(int type, int value) {
  if (currStorageOffset + 1 + 2 >= storageSize) {
    dumpSettingsToStorage();
    return;
  }
  EEPROMr.write(currStorageOffset++, type);
  EEPROMr.write(currStorageOffset++, (value >> 8) & 0xFF);
  EEPROMr.write(currStorageOffset++, value & 0xFF);
  EEPROMr.write(currStorageOffset, packetTypeEnd);
  EEPROMr.commit();
}


// Save recorded chars to a memory.
void saveStorageMemory(int memoryId) {
  if (currStorageOffset + 1 + 2 + memorySize[memoryId] >= storageSize) {
    dumpSettingsToStorage();
    return;
  }

  int type = 0;
  if (memoryId == 0) { type = packetTypeMem0; }
  else if (memoryId == 1) { type = packetTypeMem1; }
  else if (memoryId == 2) { type = packetTypeMem2; }

  EEPROMr.write(currStorageOffset++, type);
  EEPROMr.write(currStorageOffset++, (memorySize[memoryId] >> 8) & 0xFF);
  EEPROMr.write(currStorageOffset++, memorySize[memoryId] & 0xFF);

  for (size_t i=0; i<memorySize[memoryId]; i++) EEPROMr.write(currStorageOffset++, memory[memoryId][i]);
  
  EEPROMr.write(currStorageOffset, packetTypeEnd);
  EEPROMr.commit();
}


void dumpSettingsToStorage() {
  currStorageOffset = 5;
  saveStorageInt(packetTypeSpeed, ditMillis);
  saveStorageInt(packetTypeFreq, toneFreq);
  if (currKeyerMode == keyerModeVibroplex) { saveStorageEmptyPacket(packetTypeKeyerModeVibroplex); }
  else if (currKeyerMode == keyerModeStraight) { saveStorageEmptyPacket(packetTypeKeyerModeStraight); }
  if (memorySize[0]) { saveStorageMemory(0); }
  if (memorySize[1]) { saveStorageMemory(1); }
  if (memorySize[2]) { saveStorageMemory(2); }
}


// SYMBOL GENERATION

// Delay that checks for dot insertion and optionally can be interrupted
// by a pin meeting a condition. Return that pin.
int delayInterruptable(int ms, int *pins, const int *conditions, size_t numPins) {
  unsigned long finish = millis() + ms;

  while(1) {
    if (ms != -1 && millis() > finish) { return -1; }

    if (prevSymbol == symDah) {
      if (!ditDetected) { ditDetected = !digitalRead(pinKeyDit); }
    }
    for (size_t i=0; i < numPins; i++) {
      if (digitalRead(pins[i]) == conditions[i]) { return pins[i]; }
    }
  }
}


// Wait for pin to change state.
void waitPin(int pin, int condition) {
  int pins[1] = { pin };
  int conditions[1] = { condition };
  delayInterruptable(-1, pins, conditions, 1);
  delay(250); // debounce
}


// Play a symbol but watch for changed state in pin(s).
// Add char to packet for network.
int playSymInterruptableVec(int sym, int transmit, int *pins, int *conditions, size_t numPins) {

  unsigned int newGap = millis() - lastSymPlayedTime;
  if (newGap > 5) { gap = newGap + ditMillis; }

  prevSymbol = sym;

  tone(pinSpeaker, toneFreq);
  digitalWrite(pinStatusLed, HIGH);
  if (transmit) { digitalWrite(pinMosfet, HIGH); }
  
  int ret = delayInterruptable(ditMillis * (sym == symDit ? 1 : 3), pins, conditions, numPins);

  noTone(pinSpeaker);
  digitalWrite(pinStatusLed, LOW);
  digitalWrite(pinMosfet, LOW);

  if ((netMode == netClient) && transmit && (currKeyerMode == keyerModeIambic)) {
    toChar = (toChar << 2) + sym;
    toLength++;
  }

  if (ret != -1) { return ret; }

  ret = delayInterruptable(ditMillis, pins, conditions, numPins);
  if (ret != -1) { return ret; }

  lastSymPlayedTime = millis();  
  return -1;
}


void playSym(int sym, int transmit, int memoryId, int toRecord) {


  playSymInterruptableVec(sym, transmit, NULL, NULL, 0);
  if (memoryId) { memRecord(memoryId, toRecord); }
  lastSymPlayedTime = millis(); 
}


int playSymInterruptable(int sym, int transmit, int pin, int condition) {
  int pins[1] = { pin };
  int conditions[1] = { condition };
  return playSymInterruptableVec(sym, transmit, pins, conditions, 1);
}


// MORSE PLAYER FUNCTIONS

int playChar(const char oneChar, int transmit) {
  int pins[2] = { pinKeyDit, pinKeyDah };
  int conditions[2] = { LOW, LOW };
  int inchar = 0;
  int ret = 0;

  for (unsigned int j = 0; j < 8; j++) {
      int bit = morse_ascii[(int)oneChar] & (0x80 >> j);
      if (inchar) {
            ret = playSymInterruptableVec(bit + 1, transmit, pins, conditions, 2);
            if (ret != -1) {
              waitPin(ret, HIGH);
              return ret;
            }
      } else if (bit) { inchar = 1; }
 }
  delay(ditMillis * 2);
  return 0;
}


int playStr(const char *oneString, int transmit) {

  for (unsigned int j = 0; j < strlen(oneString); j++) {
    if (oneString[j] == ' ') { delay(ditMillis * 7); }
    else { 
      int ret = playChar(oneString[j], transmit);
      if (ret != 0) { return ret; }
    }
  }
  return 0;
}


int playSpeed() {

  char frame[10];
  int speed = 1200 / ditMillis;
  itoa(speed, frame, 10);
  delay(250);
  int ret = playStr(frame, SPKR);
  if (ret) { return ret; }
  delay(250);
  return 0;
}


// MEMORY RECORDING FUNCTIONS

// Record a char in the memory buffer.
void memRecord(int memoryId, int value) {
  memory[memoryId][memorySize[memoryId]] = value;
  memorySize[memoryId]++;
}


// Record a memory.
void setMemory(int memoryId, int pin, int inverted) {
  memorySize[memoryId] = 0;
  playSym(symDah, SPKR, NO_REC, 0);
  delay(50);
  playSym(symDah, SPKR, NO_REC, 0);
  delay(50);
  playSym(symDah, SPKR, NO_REC, 0);
  delay(50);
  recording = 2;
  
  while(1) {
    delay(0);
    int ditPressed = (digitalRead(pinKeyDit) == LOW);
    int dahPressed = (digitalRead(pinKeyDah) == LOW);
    delay(3);
    ditPressed = ditPressed & (digitalRead(pinKeyDit) == LOW);
    dahPressed = dahPressed & (digitalRead(pinKeyDah) == LOW);

    if ((ditPressed || dahPressed) && (millis() - lastSymPlayedTime > ditMillis)) {
      // record a space;

      if (recording == 2) {
        recording = 1;
      } else {
        double spaceDuration = (millis() - lastSymPlayedTime) / (ditMillis / 3);
        spaceDuration += 2.5;
        int toRecord = spaceDuration;
        if (toRecord > 255) { toRecord = 255; }
        memRecord(memoryId, toRecord);
      }
    }

    processPaddles(ditPressed, dahPressed, SPKR, memoryId);

    if (memorySize[memoryId] >= sizeof(memory[memoryId])-2) { break; } // protect against overflow

    if (digitalRead(pinSetup) == (inverted ? HIGH : LOW)) {
      delay(50);
      waitPin(pinSetup, inverted ? LOW : HIGH);
      break;
    }
  }
  
  saveStorageMemory(memoryId);
  recording = 0;

  tone(pinSpeaker, 1300);
  delay(300);
  tone(pinSpeaker, 900);
  delay(300);
  tone(pinSpeaker, 2000);

  for (int i=0; i<=memoryId; i++) {
    digitalWrite(pinStatusLed, HIGH);
    delay(150);
    digitalWrite(pinStatusLed, LOW);
    delay(150);
  }

  noTone(pinSpeaker);
}


// Play a memory. Build packet if needed.
void playMemory(int memoryId) {
  if (memorySize[memoryId] == 0) {
    tone(pinSpeaker, 800);
    delay(200);
    tone(pinSpeaker, 500);
    delay(300);
    noTone(pinSpeaker);
    return;
  }

  int pins[2] = { pinKeyDit, pinKeyDah };
  int conditions[2] = { LOW, LOW };
  int duration = 0;
  toSend = 0;
  toChar = 0;
  toLength = 0;
  int cmd = 0;

  for (size_t i=0; i <= memorySize[memoryId]; i++) {

    if (i == memorySize[memoryId]) { cmd = 5; }
    else { cmd = memory[memoryId][i]; }

    DEBUG_PRINT("cmd: ");
    DEBUG_PRINTLN(cmd);
    if (cmd == 0 || cmd == 1) {
      int ret = playSymInterruptableVec(cmd+1, TX, pins, conditions, 2);
      if (ret != -1) {
        waitPin(ret, HIGH);
        return;
      }
    } else if (cmd > 4)   {
      toChar = toChar << (16 - (toLength * 2));
      toSend = (toLength << 16) + toChar;
      DEBUG_PRINT("Duration sent: ");
      DEBUG_PRINTLN(duration);
      sendPacket(toSend, duration);
      lastPacketType = udpFrame;
      toSend = 0;
      toChar = 0;
      toLength = 0;
      duration = cmd - 4;
      duration *= (ditMillis / 3);
      delay(duration);
      duration += 100;
      DEBUG_PRINT("Duration calced: ");
      DEBUG_PRINTLN(duration);
    }
  }
}


// Read memory switches and countdown to record.
void checkMemoryPin(int memoryId, int pin, int inverted) {
  if (readAnalog() == pin) {
    unsigned long whenStartedPress = millis();
    int doingSet = 0;
      
    delay(5);
        
    while (readAnalog() == pin) {
      // 3 second long press to enter memory recording mode
      if (millis() > whenStartedPress + 1000) {
        playSym(symDit, SPKR, NO_REC, 0);
        delay(500);
        playSym(symDit, SPKR, NO_REC, 0);
        delay(500);
        playSym(symDit, SPKR, NO_REC, 0);
        delay(500);
        playSym(symDit, SPKR, NO_REC, 0);
//        digitalWrite(pinStatusLed, HIGH);
        doingSet = 1;
      }
    }

    digitalWrite(pinStatusLed, LOW);
    delay(50);

    if (doingSet) { setMemory(memoryId, pin, inverted); }
    else { playMemory(memoryId); }
  }
}


// PADDLE ADJUSTMENT INPUT FUNCTIONS
// These are used to adjust program parameters via the paddles.

int scaleDown(int orig, double factor, int lowerLimit) {
  int scaled = (int)((double)orig * factor);
  if (scaled == orig) { scaled--; }
  if (scaled < lowerLimit) { scaled = lowerLimit; }
  return scaled;
}


int scaleUp(int orig, double factor, int upperLimit) {
  int scaled = (int)((double)orig * factor);
  if (scaled == orig) { scaled++; }
  if (scaled > upperLimit) { scaled = upperLimit; }
  return scaled;
}


// INITIALIZATION FUNCTIONS

void factoryReset() {
  if (EEPROMr.read(3) != storageMagic1) EEPROMr.write(0, storageMagic1);
  if (EEPROMr.read(4) != storageMagic2) EEPROMr.write(1, storageMagic2);
  if (EEPROMr.read(5) != packetTypeEnd) EEPROMr.write(2, packetTypeEnd);

  currStorageOffset = 5;

  tone(pinSpeaker, 900);
  delay(300);
  tone(pinSpeaker, 600);
  delay(300);
  tone(pinSpeaker, 1500);
  delay(900);
  noTone(pinSpeaker);
}


void loadStorage() {
  // Reset the configuration byng both paddles while the keyer is started.
  // Memory layout:
  // Bytes 0, 2, 1 : Reserved for rotation library signature.
  // Bytes 3, 4 : Reserved for magic numbers.
  // Bytes 5 - 1024 : memory packets.

  int resetRequested = (digitalRead(pinKeyDit) == LOW) && (digitalRead(pinKeyDah) == LOW);

  if (resetRequested || EEPROMr.read(3) != storageMagic1 || EEPROMr.read(4) != storageMagic2) { factoryReset(); }

  currStorageOffset = 5;
  
  while (1) {
    int packetType = EEPROMr.read(currStorageOffset);
    if (packetType == packetTypeEnd) {
      break;
    } else if (packetType == packetTypeSpeed) {
      ditMillis = (EEPROMr.read(currStorageOffset+1) << 8) | EEPROMr.read(currStorageOffset+2);
      currStorageOffset += 2;
    } else if (packetType == packetTypeFreq) {
      toneFreq = (EEPROMr.read(currStorageOffset+1) << 8) | EEPROMr.read(currStorageOffset+2);
      currStorageOffset += 2;
    } else if (packetType == packetTypeKeyerModeIambic) {
      currKeyerMode = keyerModeIambic;
    } else if (packetType == packetTypeKeyerModeVibroplex) {
      currKeyerMode = keyerModeVibroplex;
    } else if (packetType == packetTypeKeyerModeStraight) {
      currKeyerMode = keyerModeStraight;
    } else if (packetType >= packetTypeMem0 && packetType <= packetTypeMem2) {
      int memoryId = 0;
      if (packetType == packetTypeMem0) { memoryId = 0; }
      if (packetType == packetTypeMem1) { memoryId = 1; }
      if (packetType == packetTypeMem2) { memoryId = 2; }
      memorySize[memoryId] = (EEPROMr.read(currStorageOffset+1) << 8) | EEPROMr.read(currStorageOffset+2);
      for (size_t i = 0; i < memorySize[memoryId]; i++) {
        memory[memoryId][i] = EEPROMr.read(currStorageOffset + 3 + i);
      }
      currStorageOffset += 2 + memorySize[memoryId];
    }

    currStorageOffset++; // packet type byte
  }
}


void setup() {
  Serial.begin(115200);

  pinMode(pinSetup, INPUT_PULLUP);
  pinMode(pinKeyDit, INPUT_PULLUP);
  pinMode(pinKeyDah, INPUT_PULLUP);
  
  pinMode(D1, OUTPUT);
  digitalWrite(D1, LOW);
  pinMode(D2, OUTPUT);
  digitalWrite(D2, LOW);
  pinMode(D3, OUTPUT);
  digitalWrite(D3, LOW); 
  pinMode(pinStatusLed, OUTPUT);
  pinMode(pinMosfet, OUTPUT);
  pinMode(pinSpeaker, OUTPUT);
  EEPROMr.size(4);                      // Create 4 memory blocks for rotation. Adjust for memory size.
  EEPROMr.begin(1024);
  loadStorage();

  playSpeed();
  delay(250);
  
#ifdef CLIENT
  netMode = netClient;
#elif SERVER
  netMode = netServer;
#else
  netmode = readAnalog();
#endif

  if (netMode == netClient || netMode == netServer) {
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      DEBUG_PRINTLN("...");
    }
    DEBUG_PRINT("WiFi connected with IP: ");
    DEBUG_PRINTLN(WiFi.localIP());
  }
  if (netMode) {
    if (udp.begin(port) == 0) { playStr("NO PORT", SPKR); }
    else if (netMode == netClient) { playChar('C', SPKR); }
         else { playChar('S', SPKR); }
  } else { playChar('R', SPKR); }
}


// SYMBOL AQUISITION FUNCTIONS

void sendPacket(unsigned int sendData, unsigned long spacing) {

  char frame[10];

  packetCount++;
  packet.number = (spacing << 16) + packetCount;
  packet.data = sendData;
  memcpy(frame, &packet, sizeof(packet));
  udp.beginPacket(host, port);
  delay(0);
  udp.write(frame, sizeof(packet));
  delay(0);
  udp.endPacket();
  delay(50);
  lastPacketSentTime = millis();
  DEBUG_PRINT("Packet Sent: ");
  DEBUG_PRINTLN(packetCount);
}


// Takes the current state of the paddles and does the right thing with it. Handles element
// comnpletion, passes along TX state, and memory location for recording.
void processPaddles(int ditPressed, int dahPressed, int transmit, int memoryId) {

  if (ditDetected) {                                                    // Insert Dit detected during
    playSym(symDit, transmit, memoryId, 0);                             // Dah play.
    ditDetected = 0;
    playAlternate = 0;
    ditPressed = 0;
  }
  if (currKeyerMode == keyerModeIambic && ditPressed && dahPressed) {   // Both paddles
    if (prevSymbol == symDah) { playSym(symDit, transmit, memoryId, 0); }
    else playSym(symDah, transmit, memoryId, 1);
    if (iambicModeB) { playAlternate = 1; }                             // Trigger element cpmpletion.
  } else if (dahPressed && currKeyerMode != keyerModeStraight) {        // Dah paddle
    if (currKeyerMode == keyerModeIambic) {
      playSym(symDah, transmit, memoryId, 1);
    } else if (currKeyerMode == keyerModeVibroplex) {
      playStraightKey(pinKeyDah);
    }
  } else if (ditPressed) {                                              // Dit paddle
    if (prevSymbol == symDit) { ditDetected = 0; }
    if (currKeyerMode == keyerModeStraight) { playStraightKey(pinKeyDit); }
    else { playSym(symDit, transmit, memoryId, 0); }
  } else {                                                              // No Paddle
    if (playAlternate) {                                                // Handle element completion.
      if (prevSymbol == symDah) { playSym(symDit, transmit, memoryId, 0); }
      else { playSym(symDah, transmit, memoryId, 1); }
      playAlternate = 0;
    }
    // If a character packet is ready and the timing is okay, send it.
    if (toChar && (netMode == netClient) && (millis() - lastSymPlayedTime > ditMillis)) {
      toChar = toChar << (16 - (toLength * 2));
      toSend = (toLength << 16) + toChar;
      sendPacket(toSend, gap);
      lastPacketType = udpFrame;
      toSend = 0;
      toChar = 0;
      toLength = 0;
    }
    prevSymbol = 0;
  }
  // If we have 8 elements stacked in the packet, send it!
  if (toLength == 8) {
    toChar = toChar << (16 - (toLength * 2));
    toSend = (toLength << 16) + toChar;
    sendPacket(toSend, gap);
    lastPacketType = udpFrame;
    toSend = 0;
    toChar = 0;
    toLength = 0;
  }
}


// Server mode - play a packet.
void playPacket(DataPacket packet) {

  int spacing = (int)(packet.number >> 16);
  DEBUG_PRINT("spacing: ");
  DEBUG_PRINTLN(spacing);
#ifdef DEBUG
  uint16_t packetNumber = (uint16_t) (packet.number & 0xFFFF);
#endif
  uint16_t frameLength = (uint16_t) (packet.data >> 16);
  uint16_t frame = (uint16_t) packet.data;
 
  int alreadyPassed = (int) (millis() - lastSymPlayedTime) - ditMillis;
  DEBUG_PRINT("Packet recd: ");
  DEBUG_PRINTLN(packetNumber);
  DEBUG_PRINT("alreadypassed: ");
  DEBUG_PRINTLN(alreadyPassed);
  if (spacing > alreadyPassed) {
    int waitTime = spacing - alreadyPassed - (ditMillis * 2);
    if (waitTime > 10) { 
      delay(waitTime); }
      DEBUG_PRINT("waittime: ");
      DEBUG_PRINTLN(waitTime);
  }
  for (int x = 0; x < frameLength; x++) {
    unsigned int roll = (frame & 0xC000) >> 14;
    frame = frame << 2;
    delay(0);
    playSym(roll, TX, NO_REC, 0);
  }
}


// See what kind of packet came in, and queue as necessary.
void parsePacket(DataPacket packet) {

  uint16_t updPacketType = packet.data >> 30;
  uint16_t frame = (uint16_t) packet.data;

  switch (updPacketType) {
    case udpKeepAlive:
      ditMillis = frame;
      sendPacket((udpAck << 30), 0);        
      if (!packets.isEmpty()) { playNextPacket = 1; }
      else playNextPacket = 0;
      break;
    case udpFrame:
      packets.push(packet);
  }
}


// MAIN FUNCTIONS

void loop() {
  char frame[10];

  int A0_switch = 0;

  int ditPressed = (digitalRead(pinKeyDit) == LOW);
  int dahPressed = (digitalRead(pinKeyDah) == LOW);
  delay(3);
  ditPressed = ditPressed & (digitalRead(pinKeyDit) == LOW);
  dahPressed = dahPressed & (digitalRead(pinKeyDah) == LOW);

  // Server mode handling
  if (netMode == netServer) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      udp.read(frame, 10);
      memcpy(&packet, frame, sizeof(packet));
      parsePacket(packet);
    }
    // if more than 2 packets queued, trigger playback.
    if (packets.size() > 2) { playNextPacket = 1; }
    if (playNextPacket && (!packets.isEmpty())) {
      packet = packets.shift();
      playPacket(packet);
    }
  } else if (currState == stateIdle) {
      A0_switch = readAnalog();

      // Client mode keepalive
      if (lastPacketSentTime && netMode == netClient) {
        toSend  = 0;
        keepAliveTimer = millis() - lastPacketSentTime;
        if (keepAliveTimer > 1000 && (!ditPressed && !dahPressed) && !toChar) {
          sendPacket((udpKeepAlive << 30) + ditMillis, 0);
          lastPacketType = udpKeepAlive;
          toSend = 0;
          lastSymPlayedTime = millis();
        }
      }

      processPaddles(ditPressed, dahPressed, TX, NO_REC);

    // Enter the speed set mode with a short press of the setup button
    if (digitalRead(pinSetup) == LOW) {
      unsigned long whenStartedPress = millis();
      int nextState = stateSettingSpeed;      

      delay(5);

      while (digitalRead(pinSetup) == LOW) {
        // 3 seconds to enter advanced configuration mode
        if (millis() > whenStartedPress + 1000) {
//          digitalWrite(pinStatusLed, HIGH);
          nextState = stateSettingTone;
          playStr("TONE", SPKR);
        }
        A0_switch = readAnalog();
        // While in speed set mode we press Memory1, it changes to paddle keyer
        if (A0_switch == 1) {
          playChar('I', SPKR);
          currKeyerMode = keyerModeIambic;
          saveStorageEmptyPacket(packetTypeKeyerModeIambic);
          waitPin(pinSetup, HIGH);
          nextState = stateIdle;
          break;
        }
        // While in speed set mode we press Memory2, it changes to straight key
        if (A0_switch == 2) {
          playChar('S', SPKR);
          currKeyerMode = keyerModeStraight;
          saveStorageEmptyPacket(packetTypeKeyerModeStraight);
          waitPin(pinSetup, HIGH);
          nextState = stateIdle;
          break;
        }
        // While in speed set mode we press Memory3, it changes to Vibroplex
        if (A0_switch == 3) {
          playChar('V', SPKR);
          currKeyerMode = keyerModeVibroplex;
          saveStorageEmptyPacket(packetTypeKeyerModeVibroplex);
          waitPin(pinSetup, HIGH);
          nextState = stateIdle;
          break;
        }
      }

      digitalWrite(pinStatusLed, LOW);
      currState = nextState;
        
      delay(50);
    }

    checkMemoryPin(0, 1, 0);
    checkMemoryPin(1, 2, 0);
    checkMemoryPin(2, 3, 0);
  } else if (currState == stateSettingSpeed) {
    if (playSymInterruptable(symDit, 0, pinSetup, LOW) != -1) {
      currState = stateIdle;
      saveStorageInt(packetTypeSpeed, ditMillis);      
      waitPin(pinSetup, HIGH);
      return;
    }
    while (ditPressed || dahPressed) {
      if (ditPressed) { ditMillis = scaleDown(ditMillis, 1/1.05, 20); }
      if (dahPressed) { ditMillis = scaleUp(ditMillis, 1.05, 800); }
      int ret = playSpeed(); 
      switch (ret) {
        case pinKeyDit:  
          ditPressed = true;
          break;
        case pinKeyDah:
          dahPressed = true;
          break;
        default:
          ditPressed = false;
          dahPressed = false;
      }
    }
  } else if (currState == stateSettingTone) {
    if (playSymInterruptable(symDit, 0, pinSetup, LOW) != -1) {
      currState = stateIdle;
      waitPin(pinSetup, HIGH);
      return;
    }
    if (ditPressed) { toneFreq = scaleDown(toneFreq, 1/1.1, 30); }
    if (dahPressed) { toneFreq = scaleUp(toneFreq, 1.1, 12500); }
    saveStorageInt(packetTypeFreq, toneFreq);
  }
}
