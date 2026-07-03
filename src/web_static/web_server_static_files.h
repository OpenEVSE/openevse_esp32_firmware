#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_charts_DcZkpMN__js_gz.h"
#include "web_server.assets_en_BSh51neB_js_gz.h"
#include "web_server.assets_es_XOhbxQwT_js_gz.h"
#include "web_server.assets_fr_BNSRsCYq_js_gz.h"
#include "web_server.assets_hu_DShpjJKb_js_gz.h"
#include "web_server.assets_index_CbPm_lLb_css_gz.h"
#include "web_server.assets_index_HrSCSTIq_js_gz.h"
#include "web_server.assets_rolldown_runtime_Bh1tDfsg_js_gz.h"
#include "web_server.assets_vendor_qwqsuqgE_js_gz.h"
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
  { "/assets/en-BSh51neB.js", CONTENT_EN_BSH51NEB_JS_GZ, sizeof(CONTENT_EN_BSH51NEB_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_BSH51NEB_JS_GZ_ETAG, true },
  { "/assets/es-XOhbxQwT.js", CONTENT_ES_XOHBXQWT_JS_GZ, sizeof(CONTENT_ES_XOHBXQWT_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_XOHBXQWT_JS_GZ_ETAG, true },
  { "/assets/fr-BNSRsCYq.js", CONTENT_FR_BNSRSCYQ_JS_GZ, sizeof(CONTENT_FR_BNSRSCYQ_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_BNSRSCYQ_JS_GZ_ETAG, true },
  { "/assets/hu-DShpjJKb.js", CONTENT_HU_DSHPJJKB_JS_GZ, sizeof(CONTENT_HU_DSHPJJKB_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_DSHPJJKB_JS_GZ_ETAG, true },
  { "/assets/index-CbPm_lLb.css", CONTENT_INDEX_CBPM_LLB_CSS_GZ, sizeof(CONTENT_INDEX_CBPM_LLB_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_CBPM_LLB_CSS_GZ_ETAG, true },
  { "/assets/index-HrSCSTIq.js", CONTENT_INDEX_HRSCSTIQ_JS_GZ, sizeof(CONTENT_INDEX_HRSCSTIQ_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX_HRSCSTIQ_JS_GZ_ETAG, true },
  { "/assets/rolldown-runtime-Bh1tDfsg.js", CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ, sizeof(CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ROLLDOWN_RUNTIME_BH1TDFSG_JS_GZ_ETAG, true },
  { "/assets/vendor-qwqsuqgE.js", CONTENT_VENDOR_QWQSUQGE_JS_GZ, sizeof(CONTENT_VENDOR_QWQSUQGE_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_VENDOR_QWQSUQGE_JS_GZ_ETAG, true },
  { "/favicon.ico", CONTENT_FAVICON_ICO, sizeof(CONTENT_FAVICON_ICO) - 1, _CONTENT_TYPE_ICO, CONTENT_FAVICON_ICO_ETAG, false },
  { "/index.html", CONTENT_INDEX_HTML_GZ, sizeof(CONTENT_INDEX_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_INDEX_HTML_GZ_ETAG, true },
  { "/manifest.webmanifest", CONTENT_MANIFEST_WEBMANIFEST, sizeof(CONTENT_MANIFEST_WEBMANIFEST) - 1, _CONTENT_TYPE_MANIFEST, CONTENT_MANIFEST_WEBMANIFEST_ETAG, false },
  { "/pwa-192x192.png", CONTENT_PWA_192X192_PNG, sizeof(CONTENT_PWA_192X192_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_192X192_PNG_ETAG, false },
  { "/pwa-512x512.png", CONTENT_PWA_512X512_PNG, sizeof(CONTENT_PWA_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_512X512_PNG_ETAG, false },
  { "/pwa-maskable-512x512.png", CONTENT_PWA_MASKABLE_512X512_PNG, sizeof(CONTENT_PWA_MASKABLE_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_MASKABLE_512X512_PNG_ETAG, false },
  { "/sw.js", CONTENT_SW_JS, sizeof(CONTENT_SW_JS) - 1, _CONTENT_TYPE_JS, CONTENT_SW_JS_ETAG, false },
};
