static const char CONTENT_SW_JS[] PROGMEM = 
  "\n"
  "self.addEventListener('install', function(e) {\n"
  "  self.skipWaiting();\n"
  "});\n"
  "self.addEventListener('activate', function(e) {\n"
  "  self.registration.unregister()\n"
  "    .then(function() {\n"
  "      return self.clients.matchAll();\n"
  "    })\n"
  "    .then(function(clients) {\n"
  "      clients.forEach(client => client.navigate(client.url))\n"
  "    });\n"
  "});\n"
  "    \n";
static const char CONTENT_SW_JS_ETAG[] PROGMEM = "cb7255cc9d9e71c4e60cf285afb52d4e1502b1c855d6b533b038da8921e482b3";
