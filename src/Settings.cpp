
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
    else if (argument == "--proxy") {
      if (++i >= argc)
        return false;
      settings.proxy_server = unquote(argv[i]);
    }
    else if (argument == "-r" || argument == "--refresh") {
      if (++i >= argc)
        return false;
      const auto policy = unquote(argv[i]);
      if (policy == "never")
        settings.refresh_policy = RefreshPolicy::never;
      else if (policy == "when-expired")
        settings.refresh_policy = RefreshPolicy::when_expired;
      else if (policy == "when-expired-async")
        settings.refresh_policy = RefreshPolicy::when_expired_async;
      else if (policy == "always")
        settings.refresh_policy = RefreshPolicy::always;
      else
        return false;
    }
    else if (argument == "--refresh-timeout") {
      if (++i >= argc)
        return false;
      const auto timeout = std::atoi(unquote(argv[i]).data());
      settings.refresh_timeout = std::chrono::seconds(timeout);
    }
    else if (argument == "--request-timeout") {
      if (++i >= argc)
        return false;
      const auto timeout = std::atoi(unquote(argv[i]).data());
      settings.request_timeout = std::chrono::seconds(timeout);
    }
    else if (argument == "--no-append") { settings.append = false; }
    else if (argument == "--no-download") { settings.download = false; }
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
    "webrecorder %s(c) 2019-2020 by Albert Kalchmair\n"
    "\n"
    "Usage: %s [-options] [url|file]\n"
    "  -u, --url <url>            set initial request URL.\n"
    "  -f, --file <file>          set input/output file.\n"
    "  -i, --input <file>         set input file.\n"
    "  -o, --output <file>        set output file.\n"
    "  -r, --refresh <mode>       refresh policy:\n"
    "                               never (default)\n"
    "                               when-expired\n"
    "                               when-expired-async\n"
    "                               always\n"
    "  --refresh-timeout <sec.>   refresh timeout (default: 1).\n"
    "  --request-timeout <sec.>   request timeout (default: 5).\n"
    "  --no-append                do not keep not requested files.\n"
    "  --no-download              do not download missing files.\n"
    "  --allow-lossy-compression  allow lossy compression of big images.\n"
    "  --block-hosts-file <file>  block hosts in file.\n"
    "  --inject-js-file <file>    inject JavaScript in every HTML file.\n"
    "  --patch-base-tag           patch base tag so URLs are relative to original host.\n"
    "  --open-browser             open browser and navigate to requested URL.\n"
    "  --proxy <host[:port]>      set a HTTP proxy.\n"
    "  -h, --help                 print this help.\n"
    "\n"
    "All Rights Reserved.\n"
    "This program comes with absolutely no warranty.\n"
    "See the GNU General Public License, version 3 for details.\n"
    "\n", version, program.c_str());
}
