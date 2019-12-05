
#include "CookieStore.h"
#include <sstream>
#include <cstring>

void CookieStore::set(const std::string& url, std::string_view cookie) {
  auto lock = std::lock_guard(m_mutex);
  const auto hostname = get_hostname(url);
  const auto equal = cookie.find('=');
  const auto key = cookie.substr(0, equal);
  const auto value = cookie.substr(equal + 1);
  m_cookies[std::string(hostname)][std::string(key)] = value;
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
  auto cookies = std::string();
  for_each_cookie(url, [&](const auto& cookie) {
    cookies += (cookies.empty() ? "" : "; ");
    cookies += cookie;
  });
  return cookies;
}
