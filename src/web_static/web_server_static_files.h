#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_charts_DcZkpMN__js_gz.h"
#include "web_server.assets_en_DKqPGW07_js_gz.h"
#include "web_server.assets_es_C0O6K_uQ_js_gz.h"
#include "web_server.assets_fr_CQenrIAy_js_gz.h"
#include "web_server.assets_hu_BG7Ch436_js_gz.h"
#include "web_server.assets_index_B5QpvRtB_js_gz.h"
#include "web_server.assets_index_BBkqEb92_css_gz.h"
#include "web_server.assets_rolldown_runtime_Bh1tDfsg_js_gz.h"
#include "web_server.assets_vendor_P1Ep0BRL_js_gz.h"
#include "web_server.favicon_ico.h"
#include "web_server.index_html_gz.h"
#include "web_server.manifest_webmanifest.h"
#include "web_server.pwa_192x192_png.h"
#include "web_server.pwa_512x512_png.h"
#include "web_server.pwa_maskable_512x512_png.h"
#include "web_server.sw_js.h"
StaticFile web_server_static_files[] = {
  { "/apple-touch-icon.png", CONTENT_APPLE_TOUCH_ICON_PNG, sizeof(CONTENT_APPLE_TOUCH_ICON_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_APPLE_TOUCH_ICON_PNG_ETAG, false },
  { "/assets/charts-CnsZ1jie.css", CONTENT_CHARTS_CNSZ1JIE_CSS_GZ, sizeof(CONTENT_CHARTS_CNSZ1JIE_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_CHARTS_CNSZ1JIE_CSS_GZ_ETAG, true },
  { "/assets/charts-DcZkpMN-.js", CONTENT_CHARTS_DCZKPMN__JS_GZ, sizeof(CONTENT_CHARTS_DCZKPMN__JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_CHARTS_DCZKPMN__JS_GZ_ETAG, true },
  { "/assets/en-DKqPGW07.js", CONTENT_EN_DKQPGW07_JS_GZ, sizeof(CONTENT_EN_DKQPGW07_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_DKQPGW07_JS_GZ_ETAG, true },
  { "/assets/es-C0O6K_uQ.js", CONTENT_ES_C0O6K_UQ_JS_GZ, sizeof(CONTENT_ES_C0O6K_UQ_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_C0O6K_UQ_JS_GZ_ETAG, true },
  { "/assets/fr-CQenrIAy.js", CONTENT_FR_CQENRIAY_JS_GZ, sizeof(CONTENT_FR_CQENRIAY_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_CQENRIAY_JS_GZ_ETAG, true },
  { "/assets/hu-BG7Ch436.js", CONTENT_HU_BG7CH436_JS_GZ, sizeof(CONTENT_HU_BG7CH436_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_BG7CH436_JS_GZ_ETAG, true },
  { "/assets/index-B5QpvRtB.js", CONTENT_INDEX_B5QPVRTB_JS_GZ, sizeof(CONTENT_INDEX_B5QPVRTB_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX_B5QPVRTB_JS_GZ_ETAG, true },
  { "/assets/index-BBkqEb92.css", CONTENT_INDEX_BBKQEB92_CSS_GZ, sizeof(CONTENT_INDEX_BBKQEB92_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_BBKQEB92_CSS_GZ_ETAG, true },
  { "/assets/rolldown-runtime-Bh1tDfsg.js", CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ, sizeof(CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ_ETAG, true },
  { "/assets/vendor-P1Ep0BRL.js", CONTENT_VENDOR_P1EP0BRL_JS_GZ, sizeof(CONTENT_VENDOR_P1EP0BRL_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_VENDOR_P1EP0BRL_JS_GZ_ETAG, true },
  { "/favicon.ico", CONTENT_FAVICON_ICO, sizeof(CONTENT_FAVICON_ICO) - 1, _CONTENT_TYPE_ICO, CONTENT_FAVICON_ICO_ETAG, false },
  { "/index.html", CONTENT_INDEX_HTML_GZ, sizeof(CONTENT_INDEX_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_INDEX_HTML_GZ_ETAG, true },
  { "/manifest.webmanifest", CONTENT_MANIFEST_WEBMANIFEST, sizeof(CONTENT_MANIFEST_WEBMANIFEST) - 1, _CONTENT_TYPE_MANIFEST, CONTENT_MANIFEST_WEBMANIFEST_ETAG, false },
  { "/pwa-192x192.png", CONTENT_PWA_192X192_PNG, sizeof(CONTENT_PWA_192X192_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_192X192_PNG_ETAG, false },
  { "/pwa-512x512.png", CONTENT_PWA_512X512_PNG, sizeof(CONTENT_PWA_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_512X512_PNG_ETAG, false },
  { "/pwa-maskable-512x512.png", CONTENT_PWA_MASKABLE_512X512_PNG, sizeof(CONTENT_PWA_MASKABLE_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_MASKABLE_512X512_PNG_ETAG, false },
  { "/sw.js", CONTENT_SW_JS, sizeof(CONTENT_SW_JS) - 1, _CONTENT_TYPE_JS, CONTENT_SW_JS_ETAG, false },
};
