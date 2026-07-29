#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_2ynqPV6l_js_gz.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_en_DLczPtx7_js_gz.h"
#include "web_server.assets_es_BBm85vGR_js_gz.h"
#include "web_server.assets_fr_BEt1e5hL_js_gz.h"
#include "web_server.assets_hu_CmfP0Gk6_js_gz.h"
#include "web_server.assets_index_C7ocsJVf_js_gz.h"
#include "web_server.assets_index_mvy7AxYT_css_gz.h"
#include "web_server.assets_rolldown_runtime_Bh1tDfsg_js_gz.h"
#include "web_server.assets_vendor_C2pyRtw5_js_gz.h"
#include "web_server.favicon_ico.h"
#include "web_server.index_html_gz.h"
#include "web_server.manifest_webmanifest.h"
#include "web_server.pwa_192x192_png.h"
#include "web_server.pwa_512x512_png.h"
#include "web_server.pwa_maskable_512x512_png.h"
#include "web_server.sw_js.h"
StaticFile web_server_static_files[] = {
  { "/apple-touch-icon.png", CONTENT_APPLE_TOUCH_ICON_PNG, sizeof(CONTENT_APPLE_TOUCH_ICON_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_APPLE_TOUCH_ICON_PNG_ETAG, false },
  { "/assets/charts-2ynqPV6l.js", CONTENT_CHARTS_2YNQPV6L_JS_GZ, sizeof(CONTENT_CHARTS_2YNQPV6L_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_CHARTS_2YNQPV6L_JS_GZ_ETAG, true },
  { "/assets/charts-CnsZ1jie.css", CONTENT_CHARTS_CNSZ1JIE_CSS_GZ, sizeof(CONTENT_CHARTS_CNSZ1JIE_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_CHARTS_CNSZ1JIE_CSS_GZ_ETAG, true },
  { "/assets/en-DLczPtx7.js", CONTENT_EN_DLCZPTX7_JS_GZ, sizeof(CONTENT_EN_DLCZPTX7_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_DLCZPTX7_JS_GZ_ETAG, true },
  { "/assets/es-BBm85vGR.js", CONTENT_ES_BBM85VGR_JS_GZ, sizeof(CONTENT_ES_BBM85VGR_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_BBM85VGR_JS_GZ_ETAG, true },
  { "/assets/fr-BEt1e5hL.js", CONTENT_FR_BET1E5HL_JS_GZ, sizeof(CONTENT_FR_BET1E5HL_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_BET1E5HL_JS_GZ_ETAG, true },
  { "/assets/hu-CmfP0Gk6.js", CONTENT_HU_CMFP0GK6_JS_GZ, sizeof(CONTENT_HU_CMFP0GK6_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_CMFP0GK6_JS_GZ_ETAG, true },
  { "/assets/index-C7ocsJVf.js", CONTENT_INDEX_C7OCSJVF_JS_GZ, sizeof(CONTENT_INDEX_C7OCSJVF_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX_C7OCSJVF_JS_GZ_ETAG, true },
  { "/assets/index-mvy7AxYT.css", CONTENT_INDEX_MVY7AXYT_CSS_GZ, sizeof(CONTENT_INDEX_MVY7AXYT_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_MVY7AXYT_CSS_GZ_ETAG, true },
  { "/assets/rolldown-runtime-Bh1tDfsg.js", CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ, sizeof(CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ_ETAG, true },
  { "/assets/vendor-C2pyRtw5.js", CONTENT_VENDOR_C2PYRTW5_JS_GZ, sizeof(CONTENT_VENDOR_C2PYRTW5_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_VENDOR_C2PYRTW5_JS_GZ_ETAG, true },
  { "/favicon.ico", CONTENT_FAVICON_ICO, sizeof(CONTENT_FAVICON_ICO) - 1, _CONTENT_TYPE_ICO, CONTENT_FAVICON_ICO_ETAG, false },
  { "/index.html", CONTENT_INDEX_HTML_GZ, sizeof(CONTENT_INDEX_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_INDEX_HTML_GZ_ETAG, true },
  { "/manifest.webmanifest", CONTENT_MANIFEST_WEBMANIFEST, sizeof(CONTENT_MANIFEST_WEBMANIFEST) - 1, _CONTENT_TYPE_MANIFEST, CONTENT_MANIFEST_WEBMANIFEST_ETAG, false },
  { "/pwa-192x192.png", CONTENT_PWA_192X192_PNG, sizeof(CONTENT_PWA_192X192_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_192X192_PNG_ETAG, false },
  { "/pwa-512x512.png", CONTENT_PWA_512X512_PNG, sizeof(CONTENT_PWA_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_512X512_PNG_ETAG, false },
  { "/pwa-maskable-512x512.png", CONTENT_PWA_MASKABLE_512X512_PNG, sizeof(CONTENT_PWA_MASKABLE_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_MASKABLE_512X512_PNG_ETAG, false },
  { "/sw.js", CONTENT_SW_JS, sizeof(CONTENT_SW_JS) - 1, _CONTENT_TYPE_JS, CONTENT_SW_JS_ETAG, false },
};
