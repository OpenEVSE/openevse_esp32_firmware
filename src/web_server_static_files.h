#include "web_server.lib.js.h"
#include "web_server.wifi_portal.htm.h"
#include "web_server.wifi_portal.js.h"
#include "web_server.home.htm.h"
#include "web_server.style.css.h"
#include "web_server.home.js.h"
StaticFile staticFiles[] = {
  { "/lib.js", CONTENT_LIB_JS, sizeof(CONTENT_LIB_JS) - 1, _CONTENT_TYPE_JS },
  { "/wifi_portal.htm", CONTENT_WIFI_PORTAL_HTM, sizeof(CONTENT_WIFI_PORTAL_HTM) - 1, _CONTENT_TYPE_HTML },
  { "/wifi_portal.js", CONTENT_WIFI_PORTAL_JS, sizeof(CONTENT_WIFI_PORTAL_JS) - 1, _CONTENT_TYPE_JS },
  { "/home.htm", CONTENT_HOME_HTM, sizeof(CONTENT_HOME_HTM) - 1, _CONTENT_TYPE_HTML },
  { "/style.css", CONTENT_STYLE_CSS, sizeof(CONTENT_STYLE_CSS) - 1, _CONTENT_TYPE_CSS },
  { "/home.js", CONTENT_HOME_JS, sizeof(CONTENT_HOME_JS) - 1, _CONTENT_TYPE_JS },
};
