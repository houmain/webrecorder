#pragma once

#include <filesystem>
#include <vector>

enum class DownloadPolicy {
  standard,         // not yet archived or expired
  always,           // ignore version of archived
  never,            // do not download
};

enum class ServePolicy {
  latest,           // wait until archived version was validated
  last_archived,    // serve archived before it was validated
  first_archived,   // serve first archived version
};

enum class ArchivePolicy {
  latest,           // update latest version of file
  first,            // do not update files
  latest_and_first, // keep first and update latest version of file
  requested,        // store only currently requested
};

struct Settings {
  bool verbose{ };
  std::string localhost{ "127.0.0.1" };
  int port{ };
  std::string url;
  std::filesystem::path input_file;
  std::filesystem::path output_file;
  std::vector<std::filesystem::path> block_hosts_files;
  std::filesystem::path inject_javascript_file;
  bool patch_base_tag{ };
  bool patch_title{ };
  std::string proxy_server;
  bool allow_lossy_compression{ };
  DownloadPolicy download_policy{ };
  ServePolicy serve_policy{ };
  ArchivePolicy archive_policy{ };
  std::chrono::seconds refresh_timeout{ 1 };
  std::chrono::seconds request_timeout{ 5 };
  bool open_browser{ };
};

bool interpret_commandline(Settings& settings, int argc, const char* argv[]);
void print_help_message(const char* argv0);
