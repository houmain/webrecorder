#pragma once

#include "common.h"
#include <regex>

class HostList;

std::string_view get_patch_script_path();
std::string_view get_patch_script();
std::string patch_absolute_url(std::string url);
std::string patch_absolute_url(std::string url, std::string_view base);
std::string_view unpatch_url(LStringView url);

class HtmlPatcher final {
public:
  HtmlPatcher(std::string server_base, std::string base_url, 
    std::string mime_type, std::string data, 
    std::string follow_link_pattern, const HostList* bypassed_hosts,
    std::string cookies, time_t start_time);

  const std::string& mime_type() const { return m_mime_type; }
  const std::string& title() const { return m_title; }
  std::string get_patched() const;

private:
  void update_base_url(std::string url);
  void parse_html();
  void parse_css();
  void add_source_set(std::string_view string);
  void add_css(std::string_view string);
  std::string_view get_link(std::string_view at) const;
  bool should_patch_absolute_url(std::string_view at, bool is_anchor) const;
  void patch_link(std::string_view at, bool is_anchor = false);
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
  const std::string m_follow_link_pattern;
  const std::regex m_follow_link_regex;
  const HostList* m_bypassed_hosts;
  const std::string m_cookies;
  const time_t m_start_time;
  std::string m_base_url;
  std::string m_title;
  std::vector<Patch> m_patches;
};
