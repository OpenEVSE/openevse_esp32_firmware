#ifndef _EMONESP_H
#define _EMONESP_H

// Uncomment to use hardware UART 1 for debug tx (GPIO2 on Huzzah) else use UART 0
#define DEBUG_SERIAL1

//------------------------------
#ifdef DEBUG_SERIAL1
#define DEBUG Serial1
#else
#define DEBUG Serial
#endif


#endif // _EMONESP_H