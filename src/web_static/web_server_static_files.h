#include "web_server.arduinoocpp_png.h"
#include "web_server.assets_js.h"
#include "web_server.emoncms_jpg.h"
#include "web_server.favicon_152_png.h"
#include "web_server.favicon_167_png.h"
#include "web_server.favicon_16x16_png.h"
#include "web_server.favicon_180_png.h"
#include "web_server.favicon_32x32_png.h"
#include "web_server.home_html_gz.h"
#include "web_server.home_js_gz.h"
#include "web_server.jquery_js_gz.h"
#include "web_server.lib_js_gz.h"
#include "web_server.localisation_js_gz.h"
#include "web_server.mqtt_png.h"
#include "web_server.ohm_jpg.h"
#include "web_server.shaper_png.h"
#include "web_server.solar_png.h"
#include "web_server.style_css_gz.h"
#include "web_server.term_html_gz.h"
#include "web_server.term_js_gz.h"
#include "web_server.wifi_portal_html_gz.h"
#include "web_server.wifi_portal_js_gz.h"
#include "web_server.wifi_signal_1_svg_gz.h"
#include "web_server.wifi_signal_2_svg_gz.h"
#include "web_server.wifi_signal_3_svg_gz.h"
#include "web_server.wifi_signal_4_svg_gz.h"
#include "web_server.wifi_signal_5_svg_gz.h"
#include "web_server.zones_json_gz.h"
StaticFile staticFiles[] = {
  { "/arduinoocpp.png", CONTENT_ARDUINOOCPP_PNG, sizeof(CONTENT_ARDUINOOCPP_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_ARDUINOOCPP_PNG_ETAG, false },
  { "/assets.js", CONTENT_ASSETS_JS, sizeof(CONTENT_ASSETS_JS) - 1, _CONTENT_TYPE_JS, CONTENT_ASSETS_JS_ETAG, false },
  { "/emoncms.jpg", CONTENT_EMONCMS_JPG, sizeof(CONTENT_EMONCMS_JPG) - 1, _CONTENT_TYPE_JPEG, CONTENT_EMONCMS_JPG_ETAG, false },
  { "/favicon-152.png", CONTENT_FAVICON_152_PNG, sizeof(CONTENT_FAVICON_152_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_FAVICON_152_PNG_ETAG, false },
  { "/favicon-167.png", CONTENT_FAVICON_167_PNG, sizeof(CONTENT_FAVICON_167_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_FAVICON_167_PNG_ETAG, false },
  { "/favicon-16x16.png", CONTENT_FAVICON_16X16_PNG, sizeof(CONTENT_FAVICON_16X16_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_FAVICON_16X16_PNG_ETAG, false },
  { "/favicon-180.png", CONTENT_FAVICON_180_PNG, sizeof(CONTENT_FAVICON_180_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_FAVICON_180_PNG_ETAG, false },
  { "/favicon-32x32.png", CONTENT_FAVICON_32X32_PNG, sizeof(CONTENT_FAVICON_32X32_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_FAVICON_32X32_PNG_ETAG, false },
  { "/home.html", CONTENT_HOME_HTML_GZ, sizeof(CONTENT_HOME_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_HOME_HTML_GZ_ETAG, true },
  { "/home.js", CONTENT_HOME_JS_GZ, sizeof(CONTENT_HOME_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HOME_JS_GZ_ETAG, true },
  { "/jquery.js", CONTENT_JQUERY_JS_GZ, sizeof(CONTENT_JQUERY_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_JQUERY_JS_GZ_ETAG, true },
  { "/lib.js", CONTENT_LIB_JS_GZ, sizeof(CONTENT_LIB_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_LIB_JS_GZ_ETAG, true },
  { "/localisation.js", CONTENT_LOCALISATION_JS_GZ, sizeof(CONTENT_LOCALISATION_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_LOCALISATION_JS_GZ_ETAG, true },
  { "/mqtt.png", CONTENT_MQTT_PNG, sizeof(CONTENT_MQTT_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_MQTT_PNG_ETAG, false },
  { "/ohm.jpg", CONTENT_OHM_JPG, sizeof(CONTENT_OHM_JPG) - 1, _CONTENT_TYPE_JPEG, CONTENT_OHM_JPG_ETAG, false },
  { "/shaper.png", CONTENT_SHAPER_PNG, sizeof(CONTENT_SHAPER_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_SHAPER_PNG_ETAG, false },
  { "/solar.png", CONTENT_SOLAR_PNG, sizeof(CONTENT_SOLAR_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_SOLAR_PNG_ETAG, false },
  { "/style.css", CONTENT_STYLE_CSS_GZ, sizeof(CONTENT_STYLE_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_STYLE_CSS_GZ_ETAG, true },
  { "/term.html", CONTENT_TERM_HTML_GZ, sizeof(CONTENT_TERM_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_TERM_HTML_GZ_ETAG, true },
  { "/term.js", CONTENT_TERM_JS_GZ, sizeof(CONTENT_TERM_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_TERM_JS_GZ_ETAG, true },
  { "/wifi_portal.html", CONTENT_WIFI_PORTAL_HTML_GZ, sizeof(CONTENT_WIFI_PORTAL_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_WIFI_PORTAL_HTML_GZ_ETAG, true },
  { "/wifi_portal.js", CONTENT_WIFI_PORTAL_JS_GZ, sizeof(CONTENT_WIFI_PORTAL_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_WIFI_PORTAL_JS_GZ_ETAG, true },
  { "/wifi_signal_1.svg", CONTENT_WIFI_SIGNAL_1_SVG_GZ, sizeof(CONTENT_WIFI_SIGNAL_1_SVG_GZ) - 1, _CONTENT_TYPE_SVG, CONTENT_WIFI_SIGNAL_1_SVG_GZ_ETAG, true },
  { "/wifi_signal_2.svg", CONTENT_WIFI_SIGNAL_2_SVG_GZ, sizeof(CONTENT_WIFI_SIGNAL_2_SVG_GZ) - 1, _CONTENT_TYPE_SVG, CONTENT_WIFI_SIGNAL_2_SVG_GZ_ETAG, true },
  { "/wifi_signal_3.svg", CONTENT_WIFI_SIGNAL_3_SVG_GZ, sizeof(CONTENT_WIFI_SIGNAL_3_SVG_GZ) - 1, _CONTENT_TYPE_SVG, CONTENT_WIFI_SIGNAL_3_SVG_GZ_ETAG, true },
  { "/wifi_signal_4.svg", CONTENT_WIFI_SIGNAL_4_SVG_GZ, sizeof(CONTENT_WIFI_SIGNAL_4_SVG_GZ) - 1, _CONTENT_TYPE_SVG, CONTENT_WIFI_SIGNAL_4_SVG_GZ_ETAG, true },
  { "/wifi_signal_5.svg", CONTENT_WIFI_SIGNAL_5_SVG_GZ, sizeof(CONTENT_WIFI_SIGNAL_5_SVG_GZ) - 1, _CONTENT_TYPE_SVG, CONTENT_WIFI_SIGNAL_5_SVG_GZ_ETAG, true },
  { "/zones.json", CONTENT_ZONES_JSON_GZ, sizeof(CONTENT_ZONES_JSON_GZ) - 1, _CONTENT_TYPE_JSON, CONTENT_ZONES_JSON_GZ_ETAG, true },
};
