#pragma once

#include <filesystem>
#include <vector>

enum class FollowLinkPolicy {
  all,
  same_top_level_domain,
  same_hostname,
  same_subpath,
  none
};

enum class ValidationPolicy {
  always,
  when_expired,
  when_expired_reload,
  never
};

struct Settings {
  std::string url;
  std::filesystem::path filename;
  std::vector<std::filesystem::path> host_block_lists;
  std::string proxy_server;
  bool write{ true };
  bool read{ true };
  bool append{ true };
  bool download{ true };
  bool open_browser{ true };
  bool filename_from_title{ false };
  bool frontend_mode{ false };
  bool verbose{ false };
  FollowLinkPolicy follow_link_policy{ FollowLinkPolicy::same_subpath };
  ValidationPolicy validation_policy{ ValidationPolicy::when_expired };
};

bool interpret_commandline(Settings& settings, int argc, const char* argv[]);
void print_help_message(std::ostream& os, const char* argv0);
