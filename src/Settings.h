#pragma once

#include <filesystem>
#include <vector>

enum class FollowLinkPolicy {
  never,
  same_domain,
  same_domain_or_subdomain,
  same_path,
  always,
};

enum class RefreshPolicy {
  never,
  when_expired,
  when_expired_async,
  always
};

struct Settings {
  std::string url;
  std::filesystem::path input_file;
  std::filesystem::path output_file;
  std::vector<std::filesystem::path> block_hosts_files;
  std::vector<std::filesystem::path> bypass_hosts_files;
  std::string proxy_server;
  bool append{ true };
  bool download{ true };
  bool open_browser{ true };
  bool filename_from_title{ false };
  bool allow_lossy_compression{ false };
  FollowLinkPolicy follow_link_policy{ FollowLinkPolicy::never };
  RefreshPolicy refresh_policy{ RefreshPolicy::never };
};

bool interpret_commandline(Settings& settings, int argc, const char* argv[]);
void print_help_message(const char* argv0);
