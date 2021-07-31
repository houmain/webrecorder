#pragma once

#include "common.h"
#include <mutex>
#include <map>

class CookieStore final {
public:
  void set(const std::string& url, std::string_view cookie);
  std::string serialize() const;

  void deserialize(std::string_view data);
  std::string get_cookies_list(const std::string& url) const;

private:
  std::string build_cookies_list(const std::string& url) const;

  mutable std::mutex m_mutex;
  std::map<std::string, std::map<std::string, std::string>> m_cookies;
  mutable std::map<std::string, std::string> m_cookies_list_cache;
};
