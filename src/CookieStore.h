#pragma once

#include "common.h"
#include <mutex>

class CookieStore final {
public:
  void set(const std::string& url, std::string_view cookie);
  std::string serialize() const;

  void deserialize(std::string_view data);
  std::string get_cookies_list(const std::string& url) const;

  template<typename F>
  void for_each_cookie(const std::string& url, F&& function) const {
    auto lock = std::lock_guard(m_mutex);
    if (auto it = m_cookies.find(get_hostname(url)); it != m_cookies.end())
      for (const auto& cookie : it->second) {
        const auto& value = cookie.second; 
        const auto semicolon = value.find(';');
        function(cookie.first + '=' + (semicolon != std::string::npos ? value.substr(0, semicolon) : value));
      }
  }

private:
  mutable std::mutex m_mutex;
  std::map<std::string, std::map<std::string, std::string>, std::less<void>> m_cookies;
};
