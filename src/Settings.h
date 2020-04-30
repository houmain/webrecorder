#pragma once

#include <filesystem>
#include <vector>

enum class RefreshPolicy {
  never,
  when_expired,
  when_expired_async,
  always
};

struct Settings {
  bool verbose{ false };
  std::string url;
  std::filesystem::path input_file;
  std::filesystem::path output_file;
  std::vector<std::filesystem::path> block_hosts_files;
  std::filesystem::path inject_javascript_file;
  bool patch_base_tag{ false };
  std::string proxy_server;
  bool append{ true };
  bool download{ true };
  bool allow_lossy_compression{ false };
  RefreshPolicy refresh_policy{ RefreshPolicy::never };
  bool open_browser{ false };
};

bool interpret_commandline(Settings& settings, int argc, const char* argv[]);
void print_help_message(const char* argv0);
