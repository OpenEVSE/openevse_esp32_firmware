#if defined(EPOXY_DUINO)

// Storage for extern globals declared by native shim headers.

#include <Update.h>
#include <ArduinoOTA.h>
#include <Esp.h>

UpdateClass Update;
ArduinoOTAClass ArduinoOTA;
EspClass ESP;

#endif
