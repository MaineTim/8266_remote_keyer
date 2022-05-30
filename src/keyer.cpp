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

const int16_t pinDebug = D1;
const int16_t pinSetup = D7;               // Press Setup (Adjust speed and tone)
const int16_t pinKeyDit = D5;              // Key, dit paddle
const int16_t pinKeyDah = D6;              // Key, dah paddle
const int16_t pinStatusLed = D4;           // Led ESP8266 builin
const int16_t pinMosfet = D0;              // Key rig jack
const int16_t pinSpeaker = D8;             // Speaker


// STATE

const int16_t stateIdle = 0;
const int16_t stateSettingSpeed = 1;
const int16_t stateSettingTone = 2;


// MODE TYPES

const int16_t keyerModeIambic = 0;
const int16_t keyerModeVibroplex = 1;
const int16_t keyerModeStraight = 2;

const int16_t netDisconnected = 0;
const int16_t netClient = 1;
const int16_t netServer = 2;


// SYMBOLS

const int16_t symDit = 1;
const int16_t symDah = 2;


// SAVE PACKET TYPES

const int16_t packetTypeEnd = 0;
const int16_t packetTypeSpeed = 1;
const int16_t packetTypeFreq = 2;
const int16_t packetTypeKeyerModeIambic = 3;
const int16_t packetTypeKeyerModeVibroplex = 4;
const int16_t packetTypeKeyerModeStraight = 5;
const int16_t packetTypeMem0 = 20;
const int16_t packetTypeMem1 = 21;
const int16_t packetTypeMem2 = 22;


// INTERNAL MEMORIES

const int16_t storageSize = 2048;
const int16_t storageMagic1 = 182;
const int16_t storageMagic2 = 97;


// CONFIG DEFAULTS

int16_t toneFreq = 700;                     // Default sidetone frequncy
int16_t ditMillis = 60;                     // Default speed
int16_t currKeyerMode = keyerModeIambic;    // Default mode
int16_t iambicModeB = 1;                    // Default iambic mode

char memory[3][600];
size_t memorySize[3];

const char* ssid = "***REMOVED***";
const char* password =  "***REMOVED***";
 
const int16_t port = 80;
const char * host = "192.168.1.132";


// RUN STATE

int16_t currState = stateIdle;
int16_t prevSymbol = 0; // 0=none, 1=dit, 2=dah
// unsigned long whenStartedPress;
int16_t recording = 0;
int16_t currStorageOffset = 0;
int16_t playAlternate = 0;                  // Mode B completion flag
int16_t ditDetected = 0;                    // Dit paddle hit during Dah play
int16_t memSwitch = 0;                      // Memory switch set by readAnalog()
int16_t pcount = 1000;
int16_t netMode = netDisconnected;
uint16_t toSend = 0;
unsigned long spaceStarted = 0;

WiFiServer wifiServer(80);
WiFiClient client;


// FORWARD DECLARATIONS

void dumpSettingsToStorage();
void processPaddles(int16_t ditPressed, int16_t dahPressed, int16_t transmit, int16_t memoryId);
void memRecord(int16_t memoryId, int16_t value);


// LOW LEVEL FUNCTIONS

// Toggle pin for debug signalling.
void pulsePin(int16_t pin, int16_t count) {
  while (count-- > 0) {
    digitalWrite(pin, HIGH);
    digitalWrite(pin, LOW);
  }
}


// Read the analog pin and assign a value to
// global memSwitch.
int16_t readAnalog() {
  int16_t value = analogRead(PIN_A0);
  if (value < 100) return 0;
  else if (value > 400 && value < 600) return 1;
  else if (value > 600 && value < 900) return 2;
  else if (value > 900) return 3;
  return(0);
}


void playStraightKey(int16_t releasePin) {
  tone(pinSpeaker, toneFreq);
  digitalWrite(pinStatusLed, HIGH);
  digitalWrite(pinMosfet, HIGH);

  while (digitalRead(releasePin) == LOW) {}
  
  noTone(pinSpeaker);
  digitalWrite(pinStatusLed, LOW);
  digitalWrite(pinMosfet, LOW);  
}


// EEPROM FUNCTIONS

void saveStorageEmptyPacket(int16_t type) {
  if (currStorageOffset + 1 >= storageSize) {
    dumpSettingsToStorage();
    return;
  }

  EEPROM.write(currStorageOffset++, type);
  EEPROM.write(currStorageOffset, packetTypeEnd);
  EEPROM.commit();
}


void saveStorageInt(int16_t type, int16_t value) {
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


void saveStorageMemory(int16_t memoryId) {
  if (currStorageOffset + 1 + 2 + memorySize[memoryId] >= storageSize) {
    dumpSettingsToStorage();
    return;
  }

  int16_t type = 0;
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
int16_t delayInterruptable(int16_t ms, int16_t *pins, const int16_t *conditions, size_t numPins) {
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


void waitPin(int16_t pin, int16_t condition) {
  int16_t pins[1] = { pin };
  int16_t conditions[1] = { condition };
  delayInterruptable(-1, pins, conditions, 1);
  delay(250); // debounce
}


int16_t playSymInterruptableVec(int16_t sym, int16_t transmit, int16_t *pins, int16_t *conditions, size_t numPins) {
  prevSymbol = sym;

  tone(pinSpeaker, toneFreq);
  digitalWrite(pinStatusLed, recording ? LOW : HIGH);
  if (transmit) digitalWrite(pinMosfet, HIGH);
  
  int16_t ret = delayInterruptable(ditMillis * (sym == symDit ? 1 : 3), pins, conditions, numPins);

  noTone(pinSpeaker);
  digitalWrite(pinStatusLed, recording ? HIGH : LOW);
  digitalWrite(pinMosfet, LOW);

  if (ret != -1) return ret;

  ret = delayInterruptable(ditMillis, pins, conditions, numPins);
  if (ret != -1) return ret;
  
  return -1;
}


void playSym(int16_t sym, int16_t transmit, int16_t memoryId, int16_t toRecord) {
  playSymInterruptableVec(sym, transmit, NULL, NULL, 0);
  if (memoryId) memRecord(memoryId, toRecord);
}


int16_t playSymInterruptable(int16_t sym, int16_t transmit, int16_t pin, int16_t condition) {
  int16_t pins[1] = { pin };
  int16_t conditions[1] = { condition };
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

void memRecord(int16_t memoryId, int16_t value) {
  memory[memoryId][memorySize[memoryId]] = value;
  memorySize[memoryId]++;
}


void setMemory(int16_t memoryId, int16_t pin, int16_t inverted) {
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
    int16_t ditPressed = (digitalRead(pinKeyDit) == LOW);
    int16_t dahPressed = (digitalRead(pinKeyDah) == LOW);

    if ((ditPressed || dahPressed) && loc_spaceStarted) {
      // record a space
      double spaceDuration = millis() - loc_spaceStarted;
      spaceDuration /= ditMillis;
      spaceDuration += 2.5;
      int16_t toRecord = spaceDuration;
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

  for (int16_t i=0; i<=memoryId; i++) {
    digitalWrite(pinStatusLed, HIGH);
    delay(150);
    digitalWrite(pinStatusLed, LOW);
    delay(150);
  }

  noTone(pinSpeaker);
}


void playMemory(int16_t memoryId) {
  if (memorySize[memoryId] == 0) {
    tone(pinSpeaker, 800);
    delay(200);
    tone(pinSpeaker, 500);
    delay(300);
    noTone(pinSpeaker);
    return;
  }

  int16_t pins[2] = { pinKeyDit, pinKeyDah };
  int16_t conditions[2] = { LOW, LOW };

  for (size_t i=0; i < memorySize[memoryId]; i++) {
    int16_t cmd = memory[memoryId][i];

    if (cmd == 0) {
      int16_t ret = playSymInterruptableVec(symDit, 1, pins, conditions, 2);
      if (ret != -1) {
        delay(10);
        waitPin(ret, HIGH);
        return;
      }
    } else if (cmd == 1) {
      int16_t ret = playSymInterruptableVec(symDah, 1, pins, conditions, 2);
      if (ret != -1) {
        delay(10);
        waitPin(ret, HIGH);
        return;
      }
    } else {
      int16_t duration = cmd - 2;
      duration *= ditMillis;
      delay(duration);
    }
  }
}


void checkMemoryPin(int16_t memoryId, int16_t pin, int16_t inverted) {
  if (readAnalog() == pin) {
    unsigned long whenStartedPress = millis();

    int16_t doingSet = 0;
      
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

int16_t scaleDown(int16_t orig, double factor, int16_t lowerLimit) {
  int16_t scaled = (int)((double)orig * factor);
  if (scaled == orig) scaled--;
  if (scaled < lowerLimit) scaled = lowerLimit;
  return scaled;
}


int16_t scaleUp(int16_t orig, double factor, int16_t upperLimit) {
  int16_t scaled = (int)((double)orig * factor);
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
  int16_t resetRequested = (digitalRead(pinSetup) == LOW);

  if (resetRequested || EEPROM.read(0) != storageMagic1 || EEPROM.read(1) != storageMagic2) factoryReset();

  currStorageOffset = 2;
  
  while (1) {
    int16_t packetType = EEPROM.read(currStorageOffset);
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
      int16_t memoryId = 0;
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
  int counter = 0;

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
      Serial.println("...");
    }
    Serial.print("WiFi connected with IP: ");
    Serial.println(WiFi.localIP());
  }
  switch (netMode) {
    case 0:
      playChar('R');
      break;
    case 1:
      while (!client.connect(host, port)) {
        if (++counter > 10) {
          playStr("NO CONN");
          goto end_case;
        }
      }
      playChar('C');
      end_case: break;
    case 2:
      wifiServer.begin();
      playChar('S');
  }
}


// SYMBOL AQUISITION FUNCTIONS

// Takes the current state of the paddles and does the right thing with it. Handles element
// comnpletion, passes along TX state, and memory location for recording.
void processPaddles(int16_t ditPressed, int16_t dahPressed, int16_t transmit, int16_t memoryId) {

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
    spaceStarted = millis();  
    prevSymbol = 0;
  }
}


// MAIN FUNCTIONS

void loop() {

  int16_t A0_switch = 0;

  int16_t ditPressed = (digitalRead(pinKeyDit) == LOW);
  int16_t dahPressed = (digitalRead(pinKeyDah) == LOW);

  if (netMode == netServer) {
    client = wifiServer.available();
    if (client) {
      while (client.connected()) {
        while (client.available() > 0) {
          char c = client.read();
          Serial.write(c);
        }
      }
      client.stop();
      Serial.println("Client disconnected");
    }
  } else if (currState == stateIdle) {
      A0_switch = readAnalog();

      if ((ditPressed || dahPressed) && spaceStarted) {
        double spaceDuration = millis() - spaceStarted;
        spaceDuration /= ditMillis;
        spaceDuration += 2.5;
        toSend = spaceDuration;
        if (toSend > 16383) toSend = 16383;
        if (netMode == netClient) {
          bitSet(toSend, 14);
          bitSet(toSend, 15);
          Serial.print(spaceDuration);
          Serial.print(" ");
          Serial.print(toSend);
          Serial.print(" ");
          playChar('E');
          client.print("E");
          toSend = 0;
        }
        spaceStarted = 0;
      }

      processPaddles(ditPressed, dahPressed, TX, NO_REC);

    // Enter the speed set mode with a short press of the setup button
    if (digitalRead(pinSetup) == LOW) {
      unsigned long whenStartedPress = millis();
      int16_t nextState = stateSettingSpeed;
      
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
