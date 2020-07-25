#include "web_server.assets.js.h"
#include "web_server.emoncms.jpg.h"
#include "web_server.favicon-16x16.png.h"
#include "web_server.favicon-32x32.png.h"
#include "web_server.home.html.h"
#include "web_server.home.js.h"
#include "web_server.lib.js.h"
#include "web_server.ohm.jpg.h"
#include "web_server.style.css.h"
#include "web_server.term.html.h"
#include "web_server.term.js.h"
#include "web_server.wifi_portal.html.h"
#include "web_server.wifi_portal.js.h"
#include "web_server.wifi_signal_1.svg.h"
#include "web_server.wifi_signal_2.svg.h"
#include "web_server.wifi_signal_3.svg.h"
#include "web_server.wifi_signal_4.svg.h"
#include "web_server.wifi_signal_5.svg.h"
#include "web_server.zones.json.h"
StaticFile staticFiles[] = {
  { "/assets.js", CONTENT_ASSETS_JS, sizeof(CONTENT_ASSETS_JS) - 1, _CONTENT_TYPE_JS },
  { "/emoncms.jpg", CONTENT_EMONCMS_JPG, sizeof(CONTENT_EMONCMS_JPG) - 1, _CONTENT_TYPE_JPEG },
  { "/favicon-16x16.png", CONTENT_FAVICON_16X16_PNG, sizeof(CONTENT_FAVICON_16X16_PNG) - 1, _CONTENT_TYPE_PNG },
  { "/favicon-32x32.png", CONTENT_FAVICON_32X32_PNG, sizeof(CONTENT_FAVICON_32X32_PNG) - 1, _CONTENT_TYPE_PNG },
  { "/home.html", CONTENT_HOME_HTML, sizeof(CONTENT_HOME_HTML) - 1, _CONTENT_TYPE_HTML },
  { "/home.js", CONTENT_HOME_JS, sizeof(CONTENT_HOME_JS) - 1, _CONTENT_TYPE_JS },
  { "/lib.js", CONTENT_LIB_JS, sizeof(CONTENT_LIB_JS) - 1, _CONTENT_TYPE_JS },
  { "/ohm.jpg", CONTENT_OHM_JPG, sizeof(CONTENT_OHM_JPG) - 1, _CONTENT_TYPE_JPEG },
  { "/style.css", CONTENT_STYLE_CSS, sizeof(CONTENT_STYLE_CSS) - 1, _CONTENT_TYPE_CSS },
  { "/term.html", CONTENT_TERM_HTML, sizeof(CONTENT_TERM_HTML) - 1, _CONTENT_TYPE_HTML },
  { "/term.js", CONTENT_TERM_JS, sizeof(CONTENT_TERM_JS) - 1, _CONTENT_TYPE_JS },
  { "/wifi_portal.html", CONTENT_WIFI_PORTAL_HTML, sizeof(CONTENT_WIFI_PORTAL_HTML) - 1, _CONTENT_TYPE_HTML },
  { "/wifi_portal.js", CONTENT_WIFI_PORTAL_JS, sizeof(CONTENT_WIFI_PORTAL_JS) - 1, _CONTENT_TYPE_JS },
  { "/wifi_signal_1.svg", CONTENT_WIFI_SIGNAL_1_SVG, sizeof(CONTENT_WIFI_SIGNAL_1_SVG) - 1, _CONTENT_TYPE_SVG },
  { "/wifi_signal_2.svg", CONTENT_WIFI_SIGNAL_2_SVG, sizeof(CONTENT_WIFI_SIGNAL_2_SVG) - 1, _CONTENT_TYPE_SVG },
  { "/wifi_signal_3.svg", CONTENT_WIFI_SIGNAL_3_SVG, sizeof(CONTENT_WIFI_SIGNAL_3_SVG) - 1, _CONTENT_TYPE_SVG },
  { "/wifi_signal_4.svg", CONTENT_WIFI_SIGNAL_4_SVG, sizeof(CONTENT_WIFI_SIGNAL_4_SVG) - 1, _CONTENT_TYPE_SVG },
  { "/wifi_signal_5.svg", CONTENT_WIFI_SIGNAL_5_SVG, sizeof(CONTENT_WIFI_SIGNAL_5_SVG) - 1, _CONTENT_TYPE_SVG },
  { "/zones.json", CONTENT_ZONES_JSON, sizeof(CONTENT_ZONES_JSON) - 1, _CONTENT_TYPE_JSON },
};
