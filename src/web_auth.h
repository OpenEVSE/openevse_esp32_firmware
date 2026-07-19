#ifndef OEVSE_WEB_AUTH_H
#define OEVSE_WEB_AUTH_H
#include <string>
#include <cstdint>

std::string session_token_mint(const std::string &secret, uint32_t exp);
bool session_token_verify(const std::string &secret, const std::string &token, uint32_t now);
bool auth_constant_time_equals(const std::string &a, const std::string &b);
std::string cookie_extract(const std::string &cookie_header, const std::string &name);
#endif
