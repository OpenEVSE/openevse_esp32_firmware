#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_charts_D9C7sDdp_js_gz.h"
#include "web_server.assets_en_D3Ktogk0_js_gz.h"
#include "web_server.assets_es_U0FOF6_O_js_gz.h"
#include "web_server.assets_fr_C6KL18Nk_js_gz.h"
#include "web_server.assets_hu_0D5v_zlJ_js_gz.h"
#include "web_server.assets_index___Gp3C_a_js_gz.h"
#include "web_server.assets_index_CRhVSz8J_css_gz.h"
#include "web_server.assets_rolldown_runtime_CbXtAM7H_js_gz.h"
#include "web_server.assets_vendor_D2E1JOXk_js_gz.h"
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
  { "/assets/charts-D9C7sDdp.js", CONTENT_CHARTS_D9C7SDDP_JS_GZ, sizeof(CONTENT_CHARTS_D9C7SDDP_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_CHARTS_D9C7SDDP_JS_GZ_ETAG, true },
  { "/assets/en-D3Ktogk0.js", CONTENT_EN_D3KTOGK0_JS_GZ, sizeof(CONTENT_EN_D3KTOGK0_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_D3KTOGK0_JS_GZ_ETAG, true },
  { "/assets/es-U0FOF6-O.js", CONTENT_ES_U0FOF6_O_JS_GZ, sizeof(CONTENT_ES_U0FOF6_O_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_U0FOF6_O_JS_GZ_ETAG, true },
  { "/assets/fr-C6KL18Nk.js", CONTENT_FR_C6KL18NK_JS_GZ, sizeof(CONTENT_FR_C6KL18NK_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_C6KL18NK_JS_GZ_ETAG, true },
  { "/assets/hu-0D5v-zlJ.js", CONTENT_HU_0D5V_ZLJ_JS_GZ, sizeof(CONTENT_HU_0D5V_ZLJ_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_0D5V_ZLJ_JS_GZ_ETAG, true },
  { "/assets/index---Gp3C-a.js", CONTENT_INDEX___GP3C_A_JS_GZ, sizeof(CONTENT_INDEX___GP3C_A_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX___GP3C_A_JS_GZ_ETAG, true },
  { "/assets/index-CRhVSz8J.css", CONTENT_INDEX_CRHVSZ8J_CSS_GZ, sizeof(CONTENT_INDEX_CRHVSZ8J_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_CRHVSZ8J_CSS_GZ_ETAG, true },
  { "/assets/rolldown-runtime-CbXtAM7H.js", CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ, sizeof(CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ_ETAG, true },
  { "/assets/vendor-D2E1JOXk.js", CONTENT_VENDOR_D2E1JOXK_JS_GZ, sizeof(CONTENT_VENDOR_D2E1JOXK_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_VENDOR_D2E1JOXK_JS_GZ_ETAG, true },
  { "/favicon.ico", CONTENT_FAVICON_ICO, sizeof(CONTENT_FAVICON_ICO) - 1, _CONTENT_TYPE_ICO, CONTENT_FAVICON_ICO_ETAG, false },
  { "/index.html", CONTENT_INDEX_HTML_GZ, sizeof(CONTENT_INDEX_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_INDEX_HTML_GZ_ETAG, true },
  { "/manifest.webmanifest", CONTENT_MANIFEST_WEBMANIFEST, sizeof(CONTENT_MANIFEST_WEBMANIFEST) - 1, _CONTENT_TYPE_MANIFEST, CONTENT_MANIFEST_WEBMANIFEST_ETAG, false },
  { "/pwa-192x192.png", CONTENT_PWA_192X192_PNG, sizeof(CONTENT_PWA_192X192_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_192X192_PNG_ETAG, false },
  { "/pwa-512x512.png", CONTENT_PWA_512X512_PNG, sizeof(CONTENT_PWA_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_512X512_PNG_ETAG, false },
  { "/pwa-maskable-512x512.png", CONTENT_PWA_MASKABLE_512X512_PNG, sizeof(CONTENT_PWA_MASKABLE_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_MASKABLE_512X512_PNG_ETAG, false },
  { "/sw.js", CONTENT_SW_JS, sizeof(CONTENT_SW_JS) - 1, _CONTENT_TYPE_JS, CONTENT_SW_JS_ETAG, false },
};
