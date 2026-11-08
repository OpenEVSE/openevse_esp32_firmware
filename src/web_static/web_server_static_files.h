#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_BYLOAqTL_js_gz.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_en_N8s84vuJ_js_gz.h"
#include "web_server.assets_es_4S7l_mCG_js_gz.h"
#include "web_server.assets_fr_CCWIs0vY_js_gz.h"
#include "web_server.assets_hu_M5HAvkBD_js_gz.h"
#include "web_server.assets_index_C3p1oI3t_js_gz.h"
#include "web_server.assets_index_DBMFWiS6_css_gz.h"
#include "web_server.assets_rolldown_runtime_WkwZ5CU__js_gz.h"
#include "web_server.assets_vendor_DaMyrnPV_js_gz.h"
#include "web_server.favicon_ico.h"
#include "web_server.index_html_gz.h"
#include "web_server.manifest_webmanifest.h"
#include "web_server.pwa_192x192_png.h"
#include "web_server.pwa_512x512_png.h"
#include "web_server.pwa_maskable_512x512_png.h"
#include "web_server.sw_js.h"
StaticFile web_server_static_files[] = {
  { "/apple-touch-icon.png", CONTENT_APPLE_TOUCH_ICON_PNG, sizeof(CONTENT_APPLE_TOUCH_ICON_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_APPLE_TOUCH_ICON_PNG_ETAG, NULL },
  { "/assets/charts-BYLOAqTL.js", CONTENT_CHARTS_BYLOAQTL_JS_GZ, sizeof(CONTENT_CHARTS_BYLOAQTL_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_CHARTS_BYLOAQTL_JS_GZ_ETAG, "gzip" },
  { "/assets/charts-CnsZ1jie.css", CONTENT_CHARTS_CNSZ1JIE_CSS_GZ, sizeof(CONTENT_CHARTS_CNSZ1JIE_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_CHARTS_CNSZ1JIE_CSS_GZ_ETAG, "gzip" },
  { "/assets/en-N8s84vuJ.js", CONTENT_EN_N8S84VUJ_JS_GZ, sizeof(CONTENT_EN_N8S84VUJ_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_N8S84VUJ_JS_GZ_ETAG, "gzip" },
  { "/assets/es-4S7l_mCG.js", CONTENT_ES_4S7L_MCG_JS_GZ, sizeof(CONTENT_ES_4S7L_MCG_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_4S7L_MCG_JS_GZ_ETAG, "gzip" },
  { "/assets/fr-CCWIs0vY.js", CONTENT_FR_CCWIS0VY_JS_GZ, sizeof(CONTENT_FR_CCWIS0VY_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_CCWIS0VY_JS_GZ_ETAG, "gzip" },
  { "/assets/hu-M5HAvkBD.js", CONTENT_HU_M5HAVKBD_JS_GZ, sizeof(CONTENT_HU_M5HAVKBD_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_M5HAVKBD_JS_GZ_ETAG, "gzip" },
  { "/assets/index-C3p1oI3t.js", CONTENT_INDEX_C3P1OI3T_JS_GZ, sizeof(CONTENT_INDEX_C3P1OI3T_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX_C3P1OI3T_JS_GZ_ETAG, "gzip" },
  { "/assets/index-DBMFWiS6.css", CONTENT_INDEX_DBMFWIS6_CSS_GZ, sizeof(CONTENT_INDEX_DBMFWIS6_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_DBMFWIS6_CSS_GZ_ETAG, "gzip" },
  { "/assets/rolldown-runtime-WkwZ5CU-.js", CONTENT_ROLLDOWN_RUNTIME_WKWZ5CU__JS_GZ, sizeof(CONTENT_ROLLDOWN_RUNTIME_WKWZ5CU__JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ROLLDOWN_RUNTIME_WKWZ5CU__JS_GZ_ETAG, "gzip" },
  { "/assets/vendor-DaMyrnPV.js", CONTENT_VENDOR_DAMYRNPV_JS_GZ, sizeof(CONTENT_VENDOR_DAMYRNPV_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_VENDOR_DAMYRNPV_JS_GZ_ETAG, "gzip" },
  { "/favicon.ico", CONTENT_FAVICON_ICO, sizeof(CONTENT_FAVICON_ICO) - 1, _CONTENT_TYPE_ICO, CONTENT_FAVICON_ICO_ETAG, NULL },
  { "/index.html", CONTENT_INDEX_HTML_GZ, sizeof(CONTENT_INDEX_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_INDEX_HTML_GZ_ETAG, "gzip" },
  { "/manifest.webmanifest", CONTENT_MANIFEST_WEBMANIFEST, sizeof(CONTENT_MANIFEST_WEBMANIFEST) - 1, _CONTENT_TYPE_MANIFEST, CONTENT_MANIFEST_WEBMANIFEST_ETAG, NULL },
  { "/pwa-192x192.png", CONTENT_PWA_192X192_PNG, sizeof(CONTENT_PWA_192X192_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_192X192_PNG_ETAG, NULL },
  { "/pwa-512x512.png", CONTENT_PWA_512X512_PNG, sizeof(CONTENT_PWA_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_512X512_PNG_ETAG, NULL },
  { "/pwa-maskable-512x512.png", CONTENT_PWA_MASKABLE_512X512_PNG, sizeof(CONTENT_PWA_MASKABLE_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_MASKABLE_512X512_PNG_ETAG, NULL },
  { "/sw.js", CONTENT_SW_JS, sizeof(CONTENT_SW_JS) - 1, _CONTENT_TYPE_JS, CONTENT_SW_JS_ETAG, NULL },
};
