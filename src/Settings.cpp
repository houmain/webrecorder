
#include "Settings.h"
#include "common.h"
#include <regex>

bool interpret_commandline(Settings& settings, int argc, const char* argv[]) {
  for (auto i = 1; i < argc; i++) {
    const auto argument = std::string_view(argv[i]);
    if (argument == "-u" || argument == "--url") {
      if (++i >= argc)
        return false;
      settings.url = url_from_input(unquote(argv[i]));
    }
    else if (argument == "-f" || argument == "--file") {
      if (++i >= argc)
        return false;
      settings.input_file = settings.output_file = utf8_to_path(unquote(argv[i]));
    }
    else if (argument == "-i" || argument == "--input") {
      if (++i >= argc)
        return false;
      settings.input_file = utf8_to_path(unquote(argv[i]));
    }
    else if (argument == "-o" || argument == "--output") {
      if (++i >= argc)
        return false;
      settings.output_file = utf8_to_path(unquote(argv[i]));
    }
    else if (argument == "-v" || argument == "--verbose") {
      settings.verbose = true;
    }
    else if (argument == "--block-hosts-file") {
      if (++i >= argc)
        return false;
      settings.block_hosts_files.push_back(utf8_to_path(unquote(argv[i])));
    }
    else if (argument == "--inject-js-file") {
      if (++i >= argc)
        return false;
      settings.inject_javascript_file = utf8_to_path(unquote(argv[i]));
    }
    else if (argument == "--patch-base-tag") {
      settings.patch_base_tag = true;
    }
    else if (argument == "--patch-title") {
      settings.patch_title = true;
    }
    else if (argument == "--proxy") {
      if (++i >= argc)
        return false;
      settings.proxy_server = unquote(argv[i]);
    }
    else if (argument == "-d" || argument == "--download") {
      if (++i >= argc)
        return false;
      const auto policy = unquote(argv[i]);
      if (policy == "standard")
        settings.download_policy = DownloadPolicy::standard;
      else if (policy == "always")
        settings.download_policy = DownloadPolicy::always;
      else if (policy == "never")
        settings.download_policy = DownloadPolicy::never;
      else
        return false;
    }
    else if (argument == "-s" || argument == "--serve") {
      if (++i >= argc)
        return false;
      const auto policy = unquote(argv[i]);
      if (policy == "latest")
        settings.serve_policy = ServePolicy::latest;
      else if (policy == "last")
        settings.serve_policy = ServePolicy::last_archived;
      else if (policy == "first")
        settings.serve_policy = ServePolicy::first_archived;
      else
        return false;
    }
    else if (argument == "-a" || argument == "--archive") {
      if (++i >= argc)
        return false;
      const auto policy = unquote(argv[i]);
      if (policy == "latest")
        settings.archive_policy = ArchivePolicy::latest;
      else if (policy == "first")
        settings.archive_policy = ArchivePolicy::first;
      else if (policy == "latest-and-first")
        settings.archive_policy = ArchivePolicy::latest_and_first;
      else if (policy == "requested")
        settings.archive_policy = ArchivePolicy::requested;
      else
        return false;
    }
    else if (argument == "--refresh-timeout") {
      if (++i >= argc)
        return false;
      const auto timeout = std::atoi(unquote(argv[i]).data());
      if (timeout <= 0)
        return false;
      settings.refresh_timeout = std::chrono::seconds(timeout);
    }
    else if (argument == "--request-timeout") {
      if (++i >= argc)
        return false;
      const auto timeout = std::atoi(unquote(argv[i]).data());
      if (timeout <= 0)
        return false;
      settings.request_timeout = std::chrono::seconds(timeout);
    }
    else if (argument == "--allow-lossy-compression") { settings.allow_lossy_compression = true; }
    else if (argument == "--open-browser") { settings.open_browser = true; }
    else if (argument == "-h" || argument == "--help") {
      return false;
    }
    else if (i == argc - 1) {
      // final argument can be url or filename
      auto error = std::error_code{ };
      auto filename = utf8_to_path(unquote(argument));
      const auto is_file = std::filesystem::exists(filename, error);
      if (settings.url.empty() && !is_file)
        settings.url = url_from_input(path_to_utf8(filename));
      else
        settings.input_file = settings.output_file = filename;

      if (!settings.url.empty() &&
          settings.input_file.empty() &&
          settings.output_file.empty())
        settings.input_file = settings.output_file =
          utf8_to_path(filename_from_url(settings.url));
    }
    else {
      return false;
    }
  }

  if (settings.input_file.empty() &&
      settings.output_file.empty())
    return false;

  return true;
}

void print_help_message(const char* argv0) {
  auto program = std::string(argv0);
  if (auto i = program.rfind('/'); i != std::string::npos)
    program = program.substr(i + 1);
  if (auto i = program.rfind('.'); i != std::string::npos)
    program = program.substr(0, i);

  const auto version =
#if __has_include("_version.h")
# include "_version.h"
  " ";
#else
  "";
#endif

  printf(
    "webrecorder %s(c) 2019-2021 by Albert Kalchmair\n"
    "\n"
    "Usage: %s [-options] [url|file]\n"
    "  -u, --url <url>            set initial request URL.\n"
    "  -f, --file <file>          set input/output file.\n"
    "  -i, --input <file>         set input file.\n"
    "  -o, --output <file>        set output file.\n"
    "  -d, --download <policy>    download policy:\n"
    "                                 standard (default)\n"
    "                                 always\n"
    "                                 never\n"
    "  -s, --serve <policy>       serve policy:\n"
    "                                 latest (default)\n"
    "                                 last\n"
    "                                 first\n"
    "  -a, --archive <policy>     archive policy:\n"
    "                                 latest (default)\n"
    "                                 first\n"
    "                                 latest-and-first\n"
    "                                 requested\n"
    "  --refresh-timeout <secs>   refresh timeout (default: 1).\n"
    "  --request-timeout <secs>   request timeout (default: 5).\n"
    "  --allow-lossy-compression  allow lossy compression of big images.\n"
    "  --block-hosts-file <file>  block hosts in file.\n"
    "  --inject-js-file <file>    inject JavaScript in every HTML file.\n"
    "  --patch-base-tag           patch base so URLs are relative to original host.\n"
    "  --open-browser             open browser and navigate to requested URL.\n"
    "  --proxy <host[:port]>      set a HTTP proxy.\n"
    "  -h, --help                 print this help.\n"
    "\n"
    "All Rights Reserved.\n"
    "This program comes with absolutely no warranty.\n"
    "See the GNU General Public License, version 3 for details.\n"
    "\n", version, program.c_str());
}
