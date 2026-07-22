#ifndef WEB_SERVER_TLS_STARTUP_H
#define WEB_SERVER_TLS_STARTUP_H

template <typename StartHttps>
bool web_server_start_https(const char *certificate, const char *private_key,
                            StartHttps start_https)
{
  if(nullptr == certificate || '\0' == certificate[0] ||
     nullptr == private_key || '\0' == private_key[0]) {
    return false;
  }

  return start_https(certificate, private_key);
}

#endif // WEB_SERVER_TLS_STARTUP_H
