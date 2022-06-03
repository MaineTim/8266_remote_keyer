// Morse Code Keyer (C) 2017 Doug Hoyte
// 2-Clause BSD License
// Modified by ea4aoj 23/04/2020
// Modified by ea4hew 15/09/2020
// Modified by K1BR 05/21/2022

// Functioning:
// Press the Setup button, enter the speed configuration mode, change the speed with the paddles, to exit press the Setup button again.
// LONG press Setup button, enters advanced configuration mode, several things can be changed:

// Long press on one of the memories to record memory, press again and it is memorized.
// Short press on one of the memories to play that memory.

// 1: The tone is changed with the paddles.
// 2: Switch to paddle handler by pressing Memory1.
// 3: Switch to staright key by pressing Memory2.
// 4: Switch to vibroplex by pressing Memory3.
// To exit the advanced configuration, press the Setup button.


// 2022-05-22 - Translate comments and configure for Platformio. Add inital Iambic Mode B code.
// 2022-05-23 - Move memory switches to A0.
// 2022-05-24 - Fixed dot completion.
// 2022-05-25 - Create processPaddles(), use it for both main loop and memory recording.
// 2022-05-26 - Add CW player, network init code (TODO: get network code working.)


#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>



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

const int pinDebug = D1;
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
const int udpSpace = 3;


// INTERNAL MEMORIES

const int storageSize = 2048;
const int storageMagic1 = 182;
const int storageMagic2 = 97;


// CONFIG DEFAULTS

int toneFreq = 700;                     // Default sidetone frequncy
unsigned int ditMillis = 60;                     // Default speed
int currKeyerMode = keyerModeIambic;    // Default mode
int iambicModeB = 1;                    // Default iambic mode

char memory[3][600];
size_t memorySize[3];

const char* ssid = "***REMOVED***";
const char* password =  "***REMOVED***";
 
const unsigned int port = 4120;
const char * host = "192.168.1.132";
// const char * host = "192.168.1.124";

WiFiUDP udp;
struct DataPacket {
  unsigned int number;
  unsigned int data;
};


// RUN STATE

int currState = stateIdle;
int prevSymbol = 0; // 0=none, 1=dit, 2=dah
// unsigned long whenStartedPress;
int recording = 0;
int currStorageOffset = 0;
int playAlternate = 0;                  // Mode B completion flag
int ditDetected = 0;                    // Dit paddle hit during Dah play
int memSwitch = 0;                      // Memory switch set by readAnalog()
int pcount = 1000;
int netMode = netDisconnected;
unsigned long spaceStarted = 0;
unsigned long milliDuration = 0;
unsigned int packetCount = 0;
double spaceDuration = 0;
unsigned int toSend = 0;
uint16_t toChar = 0;
uint16_t toLength = 0;
unsigned int sinceLast = 0;

DataPacket packet;


// FORWARD DECLARATIONS

void dumpSettingsToStorage();
void processPaddles(int ditPressed, int dahPressed, int transmit, int memoryId);
void memRecord(int memoryId, int value);


// LOW LEVEL FUNCTIONS

// Toggle pin for debug signalling.
void pulsePin(int pin, int count) {
  while (count-- > 0) {
    digitalWrite(pin, HIGH);
    digitalWrite(pin, LOW);
  }
}


// Read the analog pin and assign a value to
// global memSwitch.
int readAnalog() {
  int value = analogRead(PIN_A0);
  if (value < 100) return 0;
  else if (value > 400 && value < 600) return 1;
  else if (value > 600 && value < 900) return 2;
  else if (value > 900) return 3;
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


// EEPROM FUNCTIONS

void saveStorageEmptyPacket(int type) {
  if (currStorageOffset + 1 >= storageSize) {
    dumpSettingsToStorage();
    return;
  }

  EEPROM.write(currStorageOffset++, type);
  EEPROM.write(currStorageOffset, packetTypeEnd);
  EEPROM.commit();
}


void saveStorageInt(int type, int value) {
  if (currStorageOffset + 1 + 2 >= storageSize) {
    dumpSettingsToStorage();
    return;
  }
  EEPROM.write(currStorageOffset++, type);
  EEPROM.write(currStorageOffset++, (value >> 8) & 0xFF);
  EEPROM.write(currStorageOffset++, value & 0xFF);
  EEPROM.write(currStorageOffset, packetTypeEnd);
  EEPROM.commit();
}


void saveStorageMemory(int memoryId) {
  if (currStorageOffset + 1 + 2 + memorySize[memoryId] >= storageSize) {
    dumpSettingsToStorage();
    return;
  }

  int type = 0;
  if (memoryId == 0) type = packetTypeMem0;
  else if (memoryId == 1) type = packetTypeMem1;
  else if (memoryId == 2) type = packetTypeMem2;

  EEPROM.write(currStorageOffset++, type);
  EEPROM.write(currStorageOffset++, (memorySize[memoryId] >> 8) & 0xFF);
  EEPROM.write(currStorageOffset++, memorySize[memoryId] & 0xFF);

  for (size_t i=0; i<memorySize[memoryId]; i++) EEPROM.write(currStorageOffset++, memory[memoryId][i]);
  
  EEPROM.write(currStorageOffset, packetTypeEnd);
  EEPROM.commit();
}


void dumpSettingsToStorage() {
  currStorageOffset = 2;
  saveStorageInt(packetTypeSpeed, ditMillis);
  saveStorageInt(packetTypeFreq, toneFreq);
  if (currKeyerMode == keyerModeVibroplex) saveStorageEmptyPacket(packetTypeKeyerModeVibroplex);
  else if (currKeyerMode == keyerModeStraight) saveStorageEmptyPacket(packetTypeKeyerModeStraight);
  if (memorySize[0]) saveStorageMemory(0);
  if (memorySize[1]) saveStorageMemory(1);
  if (memorySize[2]) saveStorageMemory(2);
}


// SYMBOL GENERATION

// Delay that checks for dot insertion and optionally can be interrupted
// by a pin meeting a condition.
int delayInterruptable(int ms, int *pins, const int *conditions, size_t numPins) {
  unsigned long finish = millis() + ms;

  while(1) {
    if (ms != -1 && millis() > finish) return -1;

    if (prevSymbol == symDah) {
      if (!ditDetected) { ditDetected = !digitalRead(pinKeyDit); }
    }
    for (size_t i=0; i < numPins; i++) {
      if (digitalRead(pins[i]) == conditions[i]) return pins[i];
    }
  }
}


void waitPin(int pin, int condition) {
  int pins[1] = { pin };
  int conditions[1] = { condition };
  delayInterruptable(-1, pins, conditions, 1);
  delay(250); // debounce
}


int playSymInterruptableVec(int sym, int transmit, int *pins, int *conditions, size_t numPins) {
  prevSymbol = sym;

  tone(pinSpeaker, toneFreq);
  digitalWrite(pinStatusLed, recording ? LOW : HIGH);
  if (transmit) digitalWrite(pinMosfet, HIGH);
  
  int ret = delayInterruptable(ditMillis * (sym == symDit ? 1 : 3), pins, conditions, numPins);

  noTone(pinSpeaker);
  digitalWrite(pinStatusLed, recording ? HIGH : LOW);
  digitalWrite(pinMosfet, LOW);

  if (ret != -1) return ret;

  ret = delayInterruptable(ditMillis, pins, conditions, numPins);
  if (ret != -1) return ret;
  
  return -1;
}


void playSym(int sym, int transmit, int memoryId, int toRecord) {
  playSymInterruptableVec(sym, transmit, NULL, NULL, 0);
  if (memoryId) memRecord(memoryId, toRecord);
  if ((netMode == netClient) && transmit) {
    toChar = (toChar << 2) + sym;
    toLength++;

    DEBUG_PRINTLN(toChar);
  }
  sinceLast = millis();
}


int playSymInterruptable(int sym, int transmit, int pin, int condition) {
  int pins[1] = { pin };
  int conditions[1] = { condition };
  return playSymInterruptableVec(sym, transmit, pins, conditions, 1);
}


// MORSE PLAYER FUNCTIONS

void playChar(const char oneChar) {
  int inchar = 0;

  for (unsigned int j = 0; j < 8; j++) {
      int bit = morse_ascii[(int)oneChar] & (0x80 >> j);
      if (inchar) {
          if (bit)
            playSym(symDah, SPKR, NO_REC, 0);
          else
            playSym(symDit, SPKR, NO_REC, 0);
          if (j < 7) {
              delay(30);
          }
      } else if (bit) {
          inchar = 1;
      }
  }
  delay(100);
}


void playStr(const char *oneString) {

  for (unsigned int j = 0; j < strlen(oneString); j++) {
    if (oneString[j] == ' ')
      delay(500);
    else
      playChar(oneString[j]);
  }
}


// MEMORY RECORDING FUNCTIONS

void memRecord(int memoryId, int value) {
  memory[memoryId][memorySize[memoryId]] = value;
  memorySize[memoryId]++;
}


void setMemory(int memoryId, int pin, int inverted) {
  memorySize[memoryId] = 0;
  playSym(symDah, SPKR, NO_REC, 0);
  delay(50);
  playSym(symDah, SPKR, NO_REC, 0);
  delay(50);
  playSym(symDah, SPKR, NO_REC, 0);
  delay(50);
  digitalWrite(pinStatusLed, HIGH);
  recording = 1;

  unsigned long loc_spaceStarted = 0;
  
  while(1) {
    int ditPressed = (digitalRead(pinKeyDit) == LOW);
    int dahPressed = (digitalRead(pinKeyDah) == LOW);

    if ((ditPressed || dahPressed) && loc_spaceStarted) {
      // record a space
      double spaceDuration = millis() - loc_spaceStarted;
      spaceDuration /= ditMillis;
      spaceDuration += 2.5;
      int toRecord = spaceDuration;
      if (toRecord > 255) toRecord = 255;
      memRecord(memoryId, toRecord);
      loc_spaceStarted = 0;
    }

    processPaddles(ditPressed, dahPressed, SPKR, memoryId);

    if (prevSymbol) {
      loc_spaceStarted = millis();
      prevSymbol = 0;
    }

    if (memorySize[memoryId] >= sizeof(memory[memoryId])-2) break; // protect against overflow

    if (digitalRead(pinSetup) == (inverted ? HIGH : LOW)) {
      delay(50);
      waitPin(pinSetup, inverted ? LOW : HIGH);
      break;
    }
  }
  
  saveStorageMemory(memoryId);
  
  digitalWrite(pinStatusLed, LOW);
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

  for (size_t i=0; i < memorySize[memoryId]; i++) {
    int cmd = memory[memoryId][i];

    if (cmd == 0) {
      int ret = playSymInterruptableVec(symDit, 1, pins, conditions, 2);
      if (ret != -1) {
        delay(10);
        waitPin(ret, HIGH);
        return;
      }
    } else if (cmd == 1) {
      int ret = playSymInterruptableVec(symDah, 1, pins, conditions, 2);
      if (ret != -1) {
        delay(10);
        waitPin(ret, HIGH);
        return;
      }
    } else {
      int duration = cmd - 2;
      duration *= ditMillis;
      delay(duration);
    }
  }
}


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
        digitalWrite(pinStatusLed, HIGH);
        doingSet = 1;
      }
    }

    digitalWrite(pinStatusLed, LOW);
    delay(50);

    if (doingSet) setMemory(memoryId, pin, inverted);
    else playMemory(memoryId);
  }
}


// PADDLE ADJUSTMENT INPUT FUNCTIONS
// These are used to adjust program parameters via the paddles.

int scaleDown(int orig, double factor, int lowerLimit) {
  int scaled = (int)((double)orig * factor);
  if (scaled == orig) scaled--;
  if (scaled < lowerLimit) scaled = lowerLimit;
  return scaled;
}


int scaleUp(int orig, double factor, int upperLimit) {
  int scaled = (int)((double)orig * factor);
  if (scaled == orig) scaled++;
  if (scaled > upperLimit) scaled = upperLimit;
  return scaled;
}


// INITIALIZATION FUNCTIONS

void factoryReset() {
  if (EEPROM.read(0) != storageMagic1) EEPROM.write(0, storageMagic1);
  if (EEPROM.read(1) != storageMagic2) EEPROM.write(1, storageMagic2);
  if (EEPROM.read(2) != packetTypeEnd) EEPROM.write(2, packetTypeEnd);

  currStorageOffset = 2;

  tone(pinSpeaker, 900);
  delay(300);
  tone(pinSpeaker, 600);
  delay(300);
  tone(pinSpeaker, 1500);
  delay(900);
  noTone(pinSpeaker);
}


void loadStorage() {
  // Reset the configuration by pressing the Setup and Memory1 buttons while the keyer is turned on
  int resetRequested = (digitalRead(pinSetup) == LOW);

  if (resetRequested || EEPROM.read(0) != storageMagic1 || EEPROM.read(1) != storageMagic2) factoryReset();

  currStorageOffset = 2;
  
  while (1) {
    int packetType = EEPROM.read(currStorageOffset);
    if (packetType == packetTypeEnd) {
      break;
    } else if (packetType == packetTypeSpeed) {
      ditMillis = (EEPROM.read(currStorageOffset+1) << 8) | EEPROM.read(currStorageOffset+2);
      currStorageOffset += 2;
    } else if (packetType == packetTypeFreq) {
      toneFreq = (EEPROM.read(currStorageOffset+1) << 8) | EEPROM.read(currStorageOffset+2);
      currStorageOffset += 2;
    } else if (packetType == packetTypeKeyerModeIambic) {
      currKeyerMode = keyerModeIambic;
    } else if (packetType == packetTypeKeyerModeVibroplex) {
      currKeyerMode = keyerModeVibroplex;
    } else if (packetType == packetTypeKeyerModeStraight) {
      currKeyerMode = keyerModeStraight;
    } else if (packetType >= packetTypeMem0 && packetType <= packetTypeMem2) {
      int memoryId = 0;
      if (packetType == packetTypeMem0) memoryId = 0;
      if (packetType == packetTypeMem1) memoryId = 1;
      if (packetType == packetTypeMem2) memoryId = 2;
      memorySize[memoryId] = (EEPROM.read(currStorageOffset+1) << 8) | EEPROM.read(currStorageOffset+2);
      for (size_t i = 0; i < memorySize[memoryId]; i++) {
        memory[memoryId][i] = EEPROM.read(currStorageOffset + 3 + i);
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
  
  pinMode(pinDebug, OUTPUT);
  digitalWrite(pinDebug, LOW);
  pinMode(pinStatusLed, OUTPUT);
  pinMode(pinMosfet, OUTPUT);
  pinMode(pinSpeaker, OUTPUT);
  EEPROM. begin(1024);
  loadStorage();

  playChar('Q');
  
#ifdef CLIENT
  netMode = netClient;
#elif SERVER
  netMode = netServer;
#else
  netmode = readAnalog();
#endif

  if (netMode == netClient || netMode == netServer) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      DEBUG_PRINTLN("...");
    }
    DEBUG_PRINT("WiFi connected with IP: ");
    DEBUG_PRINTLN(WiFi.localIP());
  }
  if (netMode) {
    if (udp.begin(port) == 0)
      playStr("NO PORT");
    else if (netMode == netClient) 
        playChar('C');
      else
        playChar('S');
  } else     
playChar('R');
}


// SYMBOL AQUISITION FUNCTIONS

// Takes the current state of the paddles and does the right thing with it. Handles element
// comnpletion, passes along TX state, and memory location for recording.
void processPaddles(int ditPressed, int dahPressed, int transmit, int memoryId) {

  char frame[64];

  if (ditDetected) {
    playSym(symDit, TX, memoryId, 0);
    ditDetected = 0;
    playAlternate = 0;
    ditPressed = 0;
  }
  if (currKeyerMode == keyerModeIambic && ditPressed && dahPressed) {   // Both paddles
    if (prevSymbol == symDah) { playSym(symDit, TX, memoryId, 0); }
    else playSym(symDah, TX, memoryId, 1);
    if (iambicModeB) playAlternate = 1;
  } else if (dahPressed && currKeyerMode != keyerModeStraight) {        // Dah paddle
    if (currKeyerMode == keyerModeIambic) {
      playSym(symDah, TX, memoryId, 1);
    } else if (currKeyerMode == keyerModeVibroplex) {
      playStraightKey(pinKeyDah);
    }
  } else if (ditPressed) {                                              // Dit paddle
    if (prevSymbol == symDit) ditDetected = 0;
    if (currKeyerMode == keyerModeStraight) playStraightKey(pinKeyDit);
    else { playSym(symDit, TX, memoryId, 0); }
  } else {                                                              // No Paddle
    if (playAlternate) {
      if (prevSymbol == symDah) { playSym(symDit, TX, memoryId, 0); }
      else playSym(symDah, TX, memoryId, 1);
      playAlternate = 0;
    }
    if (spaceStarted == 0) 
      spaceStarted = millis();
    if (toChar && (netMode == netClient) && (millis() - sinceLast > ditMillis)) {
      toChar = toChar << (16 - (toLength * 2));
      toSend = (toLength << 16) + toChar;
      packetCount++;
      packet.number = packetCount;
      packet.data = toSend;
      DEBUG_PRINTLN(packetCount);
      DEBUG_PRINTHEXLN(packet.data);         
      memcpy(frame, &packet, sizeof(packet));
      udp.beginPacket(host, port);
      delay(0);
      udp.write(frame, sizeof(packet));
      delay(0);
      int result = udp.endPacket();
      delay(50);
      if (result)
        DEBUG_PRINTLN("Frame packet away...");
      else {
        DEBUG_PRINTLN("Error sending packet"); }                   
      toSend = 0;
      toChar = 0;
      toLength = 0;
      spaceStarted = 0;
    }
    prevSymbol = 0;
  }
}


void decodePacket(DataPacket packet) {

  uint16_t frameLength = (uint16_t) (packet.data >> 16);
  uint16_t frame = (uint16_t) packet.data;
  uint16_t udpPacketType = frameLength >> 14;
  unsigned int waitTime = frame;
  unsigned int alreadyPast = millis() - sinceLast;
 
  switch (udpPacketType) {
      case udpKeepAlive:
        DEBUG_PRINTLN("KeepAlive");
        ditMillis = frame;
        break;
      case udpSpace:
        DEBUG_PRINTLN("Space");
//        Serial.println(waitTime);
//        Serial.println(alreadyPast);
        if (waitTime > alreadyPast)
          delay(waitTime - alreadyPast);
        break;
      case udpFrame:
        DEBUG_PRINTLN("Frame");       
        for (int x = 0; x < frameLength; x++) {
          unsigned int roll = (frame & 0xC000) >> 14;
          frame = frame << 2;
          delay(0);
          DEBUG_PRINTBINLN(roll);
          playSym(roll, TX, NO_REC, 0);
        }
        delay(ditMillis);
  }
  if (udpPacketType != udpKeepAlive) {
    DEBUG_PRINTHEXLN(packet.data);
    DEBUG_PRINTLN(packet.number);
    DEBUG_PRINTHEXLN(frameLength);
    DEBUG_PRINTHEXLN(frame);
  }
}


// MAIN FUNCTIONS

void loop() {
  char frame[64];

  int A0_switch = 0;

  int ditPressed = (digitalRead(pinKeyDit) == LOW);
  int dahPressed = (digitalRead(pinKeyDah) == LOW);

  if (netMode == netServer) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      udp.read(frame, 64);
      memcpy(&packet, frame, sizeof(packet));
      decodePacket(packet);
    }
  } else if (currState == stateIdle) {
      A0_switch = readAnalog();

      if (spaceStarted && netMode == netClient) {
        toSend  = 0;
        uint16_t udpPacketType = 0;
        milliDuration = millis() - spaceStarted;
        if (milliDuration > 2000) {
          spaceDuration = ditMillis;  //Keep server updated on speed.
          udpPacketType = udpKeepAlive;
          delay(0);
        } else if ((ditPressed || dahPressed) && (milliDuration > ditMillis * 3)) {
          spaceDuration = milliDuration;
          udpPacketType = udpSpace;
        }
        if (udpPacketType) {
          packet.number = ++packetCount;
          packet.data = (udpPacketType << 30) + (int)spaceDuration;
          DEBUG_PRINTLN(packetCount);
          DEBUG_PRINTHEXLN(packet.data);
          memcpy(frame, &packet, sizeof(packet));
          udp.beginPacket(host, port);
          delay(0);
          udp.write(frame, sizeof(packet));
          delay(0);
          int result = udp.endPacket();
          if (milliDuration > 2000)
            delay(100);
          if (result) {
            if (udpPacketType == udpKeepAlive) DEBUG_PRINT("KeepAlive ");
            else { DEBUG_PRINT("Space "); }
            DEBUG_PRINTLN("packet away...");
          } else {
            DEBUG_PRINTLN("Error sending packet"); }                    
          toSend = 0;
          spaceStarted = 0;
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
          digitalWrite(pinStatusLed, HIGH);
          nextState = stateSettingTone;
        }
        A0_switch = readAnalog();
        // While in speed set mode we press Memory1, it changes to paddle keyer
        if (A0_switch == 1) {
          playSym(symDit, SPKR, NO_REC, 0);
          playSym(symDit, SPKR, NO_REC, 0);
          currKeyerMode = keyerModeIambic;
          saveStorageEmptyPacket(packetTypeKeyerModeIambic);
          waitPin(pinSetup, HIGH);
          nextState = stateIdle;
          break;
        }
        // While in speed set mode we press Memory2, it changes to straight key
        if (A0_switch == 2) {
          playSym(symDit, SPKR, NO_REC, 0);
          playSym(symDit, SPKR, NO_REC, 0);
          playSym(symDit, SPKR, NO_REC, 0);
          currKeyerMode = keyerModeStraight;
          saveStorageEmptyPacket(packetTypeKeyerModeStraight);
          waitPin(pinSetup, HIGH);
          nextState = stateIdle;
          break;
        }
        // While in speed set mode we press Memory3, it changes to Vibroplex
        if (A0_switch == 3) {
          playSym(symDit, SPKR, NO_REC, 0);
          playSym(symDit, SPKR, NO_REC, 0);
          playSym(symDit, SPKR, NO_REC, 0);
          playSym(symDah, SPKR, NO_REC, 0);
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
      waitPin(pinSetup, HIGH);
      return;
    }
    if (ditPressed) ditMillis = scaleDown(ditMillis, 1/1.05, 20);
    if (dahPressed) ditMillis = scaleUp(ditMillis, 1.05, 800);
    saveStorageInt(packetTypeSpeed, ditMillis);
  } else if (currState == stateSettingTone) {
    if (playSymInterruptable(symDit, 0, pinSetup, LOW) != -1) {
      currState = stateIdle;
      waitPin(pinSetup, HIGH);
      return;
    }
    if (ditPressed) toneFreq = scaleDown(toneFreq, 1/1.1, 30);
    if (dahPressed) toneFreq = scaleUp(toneFreq, 1.1, 12500);
    saveStorageInt(packetTypeFreq, toneFreq);
  }
}
