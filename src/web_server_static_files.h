#include "web_server.emoncms.jpg.h"
#include "web_server.favicon-16x16.png.h"
#include "web_server.favicon-32x32.png.h"
#include "web_server.home.htm.h"
#include "web_server.home.js.h"
#include "web_server.lib.js.h"
#include "web_server.ohm.jpg.h"
#include "web_server.style.css.h"
#include "web_server.wifi_portal.htm.h"
#include "web_server.wifi_portal.js.h"
StaticFile staticFiles[] = {
  { "/emoncms.jpg", CONTENT_EMONCMS_JPG, sizeof(CONTENT_EMONCMS_JPG) - 1, _CONTENT_TYPE_JPEG },
  { "/favicon-16x16.png", CONTENT_FAVICON_16X16_PNG, sizeof(CONTENT_FAVICON_16X16_PNG) - 1, _CONTENT_TYPE_PNG },
  { "/favicon-32x32.png", CONTENT_FAVICON_32X32_PNG, sizeof(CONTENT_FAVICON_32X32_PNG) - 1, _CONTENT_TYPE_PNG },
  { "/home.htm", CONTENT_HOME_HTM, sizeof(CONTENT_HOME_HTM) - 1, _CONTENT_TYPE_HTML },
  { "/home.js", CONTENT_HOME_JS, sizeof(CONTENT_HOME_JS) - 1, _CONTENT_TYPE_JS },
  { "/lib.js", CONTENT_LIB_JS, sizeof(CONTENT_LIB_JS) - 1, _CONTENT_TYPE_JS },
  { "/ohm.jpg", CONTENT_OHM_JPG, sizeof(CONTENT_OHM_JPG) - 1, _CONTENT_TYPE_JPEG },
  { "/style.css", CONTENT_STYLE_CSS, sizeof(CONTENT_STYLE_CSS) - 1, _CONTENT_TYPE_CSS },
  { "/wifi_portal.htm", CONTENT_WIFI_PORTAL_HTM, sizeof(CONTENT_WIFI_PORTAL_HTM) - 1, _CONTENT_TYPE_HTML },
  { "/wifi_portal.js", CONTENT_WIFI_PORTAL_JS, sizeof(CONTENT_WIFI_PORTAL_JS) - 1, _CONTENT_TYPE_JS },
};
