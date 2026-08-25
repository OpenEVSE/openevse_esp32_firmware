#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_BwRwvqTk_js_gz.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_en_B_Y3iCPN_js_gz.h"
#include "web_server.assets_es_Cwe_vIL7_js_gz.h"
#include "web_server.assets_fr_CJzBrZhV_js_gz.h"
#include "web_server.assets_hu_CPwV_O96_js_gz.h"
#include "web_server.assets_index_BUcL2CR4_js_gz.h"
#include "web_server.assets_index_Bt4FGMYY_css_gz.h"
#include "web_server.assets_rolldown_runtime_CbXtAM7H_js_gz.h"
#include "web_server.assets_vendor_BbWlGqGY_js_gz.h"
#include "web_server.favicon_ico.h"
#include "web_server.index_html_gz.h"
#include "web_server.manifest_webmanifest.h"
#include "web_server.pwa_192x192_png.h"
#include "web_server.pwa_512x512_png.h"
#include "web_server.pwa_maskable_512x512_png.h"
#include "web_server.sw_js.h"
StaticFile web_server_static_files[] = {
  { "/apple-touch-icon.png", CONTENT_APPLE_TOUCH_ICON_PNG, sizeof(CONTENT_APPLE_TOUCH_ICON_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_APPLE_TOUCH_ICON_PNG_ETAG, false },
  { "/assets/charts-BwRwvqTk.js", CONTENT_CHARTS_BWRWVQTK_JS_GZ, sizeof(CONTENT_CHARTS_BWRWVQTK_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_CHARTS_BWRWVQTK_JS_GZ_ETAG, true },
  { "/assets/charts-CnsZ1jie.css", CONTENT_CHARTS_CNSZ1JIE_CSS_GZ, sizeof(CONTENT_CHARTS_CNSZ1JIE_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_CHARTS_CNSZ1JIE_CSS_GZ_ETAG, true },
  { "/assets/en-B-Y3iCPN.js", CONTENT_EN_B_Y3ICPN_JS_GZ, sizeof(CONTENT_EN_B_Y3ICPN_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_B_Y3ICPN_JS_GZ_ETAG, true },
  { "/assets/es-Cwe-vIL7.js", CONTENT_ES_CWE_VIL7_JS_GZ, sizeof(CONTENT_ES_CWE_VIL7_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_CWE_VIL7_JS_GZ_ETAG, true },
  { "/assets/fr-CJzBrZhV.js", CONTENT_FR_CJZBRZHV_JS_GZ, sizeof(CONTENT_FR_CJZBRZHV_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_CJZBRZHV_JS_GZ_ETAG, true },
  { "/assets/hu-CPwV_O96.js", CONTENT_HU_CPWV_O96_JS_GZ, sizeof(CONTENT_HU_CPWV_O96_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_CPWV_O96_JS_GZ_ETAG, true },
  { "/assets/index-BUcL2CR4.js", CONTENT_INDEX_BUCL2CR4_JS_GZ, sizeof(CONTENT_INDEX_BUCL2CR4_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX_BUCL2CR4_JS_GZ_ETAG, true },
  { "/assets/index-Bt4FGMYY.css", CONTENT_INDEX_BT4FGMYY_CSS_GZ, sizeof(CONTENT_INDEX_BT4FGMYY_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_BT4FGMYY_CSS_GZ_ETAG, true },
  { "/assets/rolldown-runtime-CbXtAM7H.js", CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ, sizeof(CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ_ETAG, true },
  { "/assets/vendor-BbWlGqGY.js", CONTENT_VENDOR_BBWLGQGY_JS_GZ, sizeof(CONTENT_VENDOR_BBWLGQGY_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_VENDOR_BBWLGQGY_JS_GZ_ETAG, true },
  { "/favicon.ico", CONTENT_FAVICON_ICO, sizeof(CONTENT_FAVICON_ICO) - 1, _CONTENT_TYPE_ICO, CONTENT_FAVICON_ICO_ETAG, false },
  { "/index.html", CONTENT_INDEX_HTML_GZ, sizeof(CONTENT_INDEX_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_INDEX_HTML_GZ_ETAG, true },
  { "/manifest.webmanifest", CONTENT_MANIFEST_WEBMANIFEST, sizeof(CONTENT_MANIFEST_WEBMANIFEST) - 1, _CONTENT_TYPE_MANIFEST, CONTENT_MANIFEST_WEBMANIFEST_ETAG, false },
  { "/pwa-192x192.png", CONTENT_PWA_192X192_PNG, sizeof(CONTENT_PWA_192X192_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_192X192_PNG_ETAG, false },
  { "/pwa-512x512.png", CONTENT_PWA_512X512_PNG, sizeof(CONTENT_PWA_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_512X512_PNG_ETAG, false },
  { "/pwa-maskable-512x512.png", CONTENT_PWA_MASKABLE_512X512_PNG, sizeof(CONTENT_PWA_MASKABLE_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_MASKABLE_512X512_PNG_ETAG, false },
  { "/sw.js", CONTENT_SW_JS, sizeof(CONTENT_SW_JS) - 1, _CONTENT_TYPE_JS, CONTENT_SW_JS_ETAG, false },
};
