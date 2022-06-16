#ifdef DEBUG
  #define DEBUG_PRINT(x)     Serial.print (x)
  #define DEBUG_PRINTDEC(x)     Serial.print (x, DEC)
  #define DEBUG_PRINTHEX(x)     Serial.print (x, HEX)
  #define DEBUG_PRINTBIN(x)     Serial.print (x, BIN)
  #define DEBUG_PRINTLN(x)  Serial.println (x)
  #define DEBUG_PRINTDECLN(x)     Serial.println (x, DEC)
  #define DEBUG_PRINTHEXLN(x)     Serial.println (x, HEX)
  #define DEBUG_PRINTBINLN(x)     Serial.println (x, BIN)

  #define DEBUG_PRINT_DOUBLE(x, y)    Serial.print(x, y)
  #define DEBUG_PRINTLN_DOUBLE(x, y)    Serial.println(x, y)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTDEC(x)
  #define DEBUG_PRINTHEX(x)
  #define DEBUG_PRINTBIN(x)
  #define DEBUG_PRINTLN(x) 
  #define DEBUG_PRINTDECLN(x)
  #define DEBUG_PRINTHEXLN(x)
  #define DEBUG_PRINTBINLN(x)

  #define DEBUG_PRINT_DOUBLE(x, y)
  #define DEBUG_PRINTLN_DOUBLE(x, y)
#endif

