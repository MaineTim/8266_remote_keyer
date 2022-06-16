#ifdef DEBUG_PIN
    #define PINHIGH(x)  GPOS = (1 << x)
    #define PINLOW(x)   GPOC = (1 << x)
    #define PINFLIP(x)\
        GPOS = (1 << x);\
        GPOC = (1 << x)
#else
    #define PINHIGH(x)
    #define PINLOW(x)
    #define PINFLIP(x)
#endif
