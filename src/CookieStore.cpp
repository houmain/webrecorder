
#include "CookieStore.h"
#include <sstream>
#include <cstring>

void CookieStore::set(const std::string& url, std::string_view cookie) {
  auto lock = std::lock_guard(m_mutex);
  const auto hostname = std::string(get_hostname(url));
  const auto equal = cookie.find('=');
  const auto key = cookie.substr(0, equal);
  const auto value = cookie.substr(equal + 1);
  m_cookies[hostname][std::string(key)] = value;
  m_cookies_list_cache.erase(hostname);
}

std::string CookieStore::serialize() const {
  auto lock = std::lock_guard(m_mutex);
  auto ss = std::ostringstream();
  for (const auto& [url, cookies] : m_cookies) {
    ss << url << '\r' << '\n';
    for (const auto& [key, value] : cookies)
      ss << '\t' << key << '=' << value << '\r' << '\n';
  }
  return ss.str();
}

void CookieStore::deserialize(std::string_view data) {
  auto lock = std::lock_guard(m_mutex);
  m_cookies.clear();

  auto* cookies = std::add_pointer_t<std::map<std::string, std::string>>{ };
  const auto end = data.end();
  for (auto it = data.begin(); it != end; it += 2) {
    const auto line_begin = it;

    auto equal = end;
    for (; it != end; ++it) {
      if (*it == '=' && equal == end)
        equal = it;

      if (it + 1 < end && it[0] == '\r' && it[1] == '\n')
        break;
    }
    const auto line_end = it;

    if (*line_begin != '\t') {
      cookies = &m_cookies[{ line_begin, line_end }];
    }
    else if (cookies && equal != end) {
      cookies->emplace(
        std::string(line_begin + 1, equal),
        std::string(equal + 1, line_end));
    }
  }
}

std::string CookieStore::get_cookies_list(const std::string& url) const {
  auto lock = std::lock_guard(m_mutex);
  const auto hostname = std::string(get_hostname(url));
  auto it = m_cookies_list_cache.find(hostname);
  if (it == end(m_cookies_list_cache))
    it = m_cookies_list_cache.emplace(hostname, build_cookies_list(hostname)).first;
  return it->second;
}

std::string CookieStore::build_cookies_list(const std::string& hostname) const {
  auto cookies = std::ostringstream();
  if (auto it = m_cookies.find(hostname); it != m_cookies.end()) {
    auto first = true;
    for (const auto& cookie : it->second) {
      const auto& value = cookie.second;
      const auto semicolon = value.find(';');
      if (!std::exchange(first, false))
        cookies << "; ";
      cookies << cookie.first << '=' <<
        (semicolon != std::string::npos ? value.substr(0, semicolon) : value);
    }
  }
  return cookies.str();
}
