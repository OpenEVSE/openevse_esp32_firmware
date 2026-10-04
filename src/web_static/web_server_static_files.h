#include "web_server.apple_touch_icon_png.h"
#include "web_server.assets_charts_CnsZ1jie_css_gz.h"
#include "web_server.assets_charts_D9C7sDdp_js_gz.h"
#include "web_server.assets_en_CEK83aYq_js_gz.h"
#include "web_server.assets_es_DP3Pux_A_js_gz.h"
#include "web_server.assets_fr_B9AbLkLp_js_gz.h"
#include "web_server.assets_hu_D7apohZy_js_gz.h"
#include "web_server.assets_index_CZW1oIH2_js_gz.h"
#include "web_server.assets_index_CteYX6T__css_gz.h"
#include "web_server.assets_rolldown_runtime_CbXtAM7H_js_gz.h"
#include "web_server.assets_vendor_C8ojbjLA_js_gz.h"
#include "web_server.favicon_ico.h"
#include "web_server.index_html_gz.h"
#include "web_server.manifest_webmanifest.h"
#include "web_server.pwa_192x192_png.h"
#include "web_server.pwa_512x512_png.h"
#include "web_server.pwa_maskable_512x512_png.h"
#include "web_server.sw_js.h"
StaticFile web_server_static_files[] = {
  { "/apple-touch-icon.png", CONTENT_APPLE_TOUCH_ICON_PNG, sizeof(CONTENT_APPLE_TOUCH_ICON_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_APPLE_TOUCH_ICON_PNG_ETAG, NULL },
  { "/assets/charts-CnsZ1jie.css", CONTENT_CHARTS_CNSZ1JIE_CSS_GZ, sizeof(CONTENT_CHARTS_CNSZ1JIE_CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_CHARTS_CNSZ1JIE_CSS_GZ_ETAG, "gzip" },
  { "/assets/charts-D9C7sDdp.js", CONTENT_CHARTS_D9C7SDDP_JS_GZ, sizeof(CONTENT_CHARTS_D9C7SDDP_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_CHARTS_D9C7SDDP_JS_GZ_ETAG, "gzip" },
  { "/assets/en-CEK83aYq.js", CONTENT_EN_CEK83AYQ_JS_GZ, sizeof(CONTENT_EN_CEK83AYQ_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_EN_CEK83AYQ_JS_GZ_ETAG, "gzip" },
  { "/assets/es-DP3Pux_A.js", CONTENT_ES_DP3PUX_A_JS_GZ, sizeof(CONTENT_ES_DP3PUX_A_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ES_DP3PUX_A_JS_GZ_ETAG, "gzip" },
  { "/assets/fr-B9AbLkLp.js", CONTENT_FR_B9ABLKLP_JS_GZ, sizeof(CONTENT_FR_B9ABLKLP_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_FR_B9ABLKLP_JS_GZ_ETAG, "gzip" },
  { "/assets/hu-D7apohZy.js", CONTENT_HU_D7APOHZY_JS_GZ, sizeof(CONTENT_HU_D7APOHZY_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_HU_D7APOHZY_JS_GZ_ETAG, "gzip" },
  { "/assets/index-CZW1oIH2.js", CONTENT_INDEX_CZW1OIH2_JS_GZ, sizeof(CONTENT_INDEX_CZW1OIH2_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_INDEX_CZW1OIH2_JS_GZ_ETAG, "gzip" },
  { "/assets/index-CteYX6T_.css", CONTENT_INDEX_CTEYX6T__CSS_GZ, sizeof(CONTENT_INDEX_CTEYX6T__CSS_GZ) - 1, _CONTENT_TYPE_CSS, CONTENT_INDEX_CTEYX6T__CSS_GZ_ETAG, "gzip" },
  { "/assets/rolldown-runtime-CbXtAM7H.js", CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ, sizeof(CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_ROLLDOWN_RUNTIME_CBXTAM7H_JS_GZ_ETAG, "gzip" },
  { "/assets/vendor-C8ojbjLA.js", CONTENT_VENDOR_C8OJBJLA_JS_GZ, sizeof(CONTENT_VENDOR_C8OJBJLA_JS_GZ) - 1, _CONTENT_TYPE_JS, CONTENT_VENDOR_C8OJBJLA_JS_GZ_ETAG, "gzip" },
  { "/favicon.ico", CONTENT_FAVICON_ICO, sizeof(CONTENT_FAVICON_ICO) - 1, _CONTENT_TYPE_ICO, CONTENT_FAVICON_ICO_ETAG, NULL },
  { "/index.html", CONTENT_INDEX_HTML_GZ, sizeof(CONTENT_INDEX_HTML_GZ) - 1, _CONTENT_TYPE_HTML, CONTENT_INDEX_HTML_GZ_ETAG, "gzip" },
  { "/manifest.webmanifest", CONTENT_MANIFEST_WEBMANIFEST, sizeof(CONTENT_MANIFEST_WEBMANIFEST) - 1, _CONTENT_TYPE_MANIFEST, CONTENT_MANIFEST_WEBMANIFEST_ETAG, NULL },
  { "/pwa-192x192.png", CONTENT_PWA_192X192_PNG, sizeof(CONTENT_PWA_192X192_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_192X192_PNG_ETAG, NULL },
  { "/pwa-512x512.png", CONTENT_PWA_512X512_PNG, sizeof(CONTENT_PWA_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_512X512_PNG_ETAG, NULL },
  { "/pwa-maskable-512x512.png", CONTENT_PWA_MASKABLE_512X512_PNG, sizeof(CONTENT_PWA_MASKABLE_512X512_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_PWA_MASKABLE_512X512_PNG_ETAG, NULL },
  { "/sw.js", CONTENT_SW_JS, sizeof(CONTENT_SW_JS) - 1, _CONTENT_TYPE_JS, CONTENT_SW_JS_ETAG, NULL },
};
