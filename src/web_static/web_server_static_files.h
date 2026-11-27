#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_BYLOAqTL_js_gz.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_en_V3_k77OE_js_gz.h"
#include "web_server.assets_es_CxEqPjmN_js_gz.h"
#include "web_server.assets_fr_G1Kv6ESU_js_gz.h"
#include "web_server.assets_hu_B29BUSau_js_gz.h"
#include "web_server.assets_index_BqCseU_F_css_gz.h"
#include "web_server.assets_index_DDC_0006_js_gz.h"
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
  { "/assets/en-V3-k77OE.js", CONTENT_EN_V3_K77OE_JS_GZ, sizeof(CONTENT_EN_V3_K77OE_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_V3_K77OE_JS_GZ_ETAG, "gzip" },
  { "/assets/es-CxEqPjmN.js", CONTENT_ES_CXEQPJMN_JS_GZ, sizeof(CONTENT_ES_CXEQPJMN_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_CXEQPJMN_JS_GZ_ETAG, "gzip" },
  { "/assets/fr-G1Kv6ESU.js", CONTENT_FR_G1KV6ESU_JS_GZ, sizeof(CONTENT_FR_G1KV6ESU_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_G1KV6ESU_JS_GZ_ETAG, "gzip" },
  { "/assets/hu-B29BUSau.js", CONTENT_HU_B29BUSAU_JS_GZ, sizeof(CONTENT_HU_B29BUSAU_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_B29BUSAU_JS_GZ_ETAG, "gzip" },
  { "/assets/index-BqCseU_F.css", CONTENT_INDEX_BQCSEU_F_CSS_GZ, sizeof(CONTENT_INDEX_BQCSEU_F_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_BQCSEU_F_CSS_GZ_ETAG, "gzip" },
  { "/assets/index-DDC_0006.js", CONTENT_INDEX_DDC_0006_JS_GZ, sizeof(CONTENT_INDEX_DDC_0006_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX_DDC_0006_JS_GZ_ETAG, "gzip" },
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
