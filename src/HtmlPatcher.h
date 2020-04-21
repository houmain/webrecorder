#pragma once

#include "common.h"

class HtmlPatcher final {
public:
  HtmlPatcher(std::string server_base,
    std::string base_url,
    std::string data,
    std::string inject_js_path,
    std::string cookies,
    time_t response_time);

  std::string get_patched() const;

private:
  void update_base_url(std::string url);
  void parse_html();
  std::string_view get_link(std::string_view at) const;
  void inject_base(std::string_view at);
  void apply_base(std::string_view at);
  void inject_patch_script(std::string_view at);
  void remove_region(std::string_view at);
  void patch(std::string_view at, std::string patch);

  struct Patch {
    std::string_view replace;
    std::string patch;
  };

  const std::string m_server_base;
  const std::string m_mime_type;
  const std::string m_data;
  const std::string m_cookies;
  const std::string m_inject_js_path;
  const time_t m_response_time;
  std::string m_base_url;
  std::vector<Patch> m_patches;
};
