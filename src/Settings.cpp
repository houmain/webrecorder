
#include "Settings.h"
#include "HtmlPatcher.h"
#include <regex>

bool interpret_commandline(Settings& settings, int argc, const char* argv[]) {
  for (auto i = 1; i < argc; i++) {
    const auto argument = std::string_view(argv[i]);
    if (argument == "-u" || argument == "--url") {
      if (++i >= argc)
        return false;
      settings.url = url_from_input(std::string(unquote(argv[i])));
    }
    else if (argument == "-f" || argument == "--file") {
      if (++i >= argc)
        return false;
      settings.input_file = settings.output_file =
        std::filesystem::u8path(unquote(argv[i]));
    }
    else if (argument == "-i" || argument == "--input") {
      if (++i >= argc)
        return false;
      settings.input_file = std::filesystem::u8path(unquote(argv[i]));
    }
    else if (argument == "-o" || argument == "--output") {
      if (++i >= argc)
        return false;
      settings.output_file = std::filesystem::u8path(unquote(argv[i]));
    }
    else if (argument == "--block-hosts-file") {
      if (++i >= argc)
        return false;
      settings.block_hosts_files.push_back(
        std::filesystem::u8path(unquote(argv[i])));
    }
    else if (argument == "--bypass-hosts-file") {
      if (++i >= argc)
        return false;
      settings.bypass_hosts_files.push_back(
        std::filesystem::u8path(unquote(argv[i])));
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
    else if (argument == "-l" || argument == "--follow-link") {
      if (++i >= argc)
        return false;
      auto policy = unquote(argv[i]);
      if (policy == "never")
        settings.follow_link_policy = FollowLinkPolicy::never;
      else if (policy == "same-domain")
        settings.follow_link_policy = FollowLinkPolicy::same_domain;
      else if (policy == "same-domain-or-subdomain")
        settings.follow_link_policy = FollowLinkPolicy::same_domain_or_subdomain;
      else if (policy == "always")
        settings.follow_link_policy = FollowLinkPolicy::always;
      else
        return false;
    }
    else if (argument == "--no-append") { settings.append = false; }
    else if (argument == "--no-download") { settings.download = false; }
    else if (argument == "--no-open-browser") { settings.open_browser = false; }
    else if (argument == "--filename-from-title") { settings.filename_from_title = true; }
    else if (argument == "--allow-lossy-compression") { settings.allow_lossy_compression = true; }
    else if (argument == "--deterministic-js") { settings.deterministic_js = true; }
    else if (argument == "-h" || argument == "--help") {
      return false;
    }
    else if (i == argc - 1) {
      // final argument can be url or filename
      auto error = std::error_code{ };
      auto filename = std::filesystem::u8path(unquote(argument));
      const auto is_file = std::filesystem::exists(filename, error);
      if (settings.url.empty() && !is_file)
        settings.url = url_from_input(filename.u8string());
      else
        settings.input_file = settings.output_file = filename;

      if (!settings.url.empty() &&
          settings.input_file.empty() &&
          settings.output_file.empty())
        settings.input_file = settings.output_file =
          std::filesystem::u8path(filename_from_url(settings.url));
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
#endif
    "";

  printf(
    "webrecorder %s (c) 2019-2020 by Albert Kalchmair\n"
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
    "  -l, --follow-link <mode>   follow link policy:\n"
    "                               never (default)\n"
    "                               same-domain\n"
    "                               same-domain-or-subdomain\n"
    "                               same-path\n"
    "                               always\n"
    "  --no-append                do not keep not requested files.\n"
    "  --no-download              do not download missing files.\n"
    "  --no-open-browser          do not open browser window.\n"
    "  --filename-from-title      generate output filename from title.\n"
    "  --allow-lossy-compression  allow lossy compression of big images.\n"
    "  --deterministic-js         make JavaScript more deterministic.\n"
    "  --block-hosts-file <file>  block hosts in file.\n"
    "  --bypass-hosts-file <file> bypass hosts in file.\n"
    "  --proxy <host[:port]>      set a HTTP proxy.\n"
    "  -h, --help  print this help.\n"
    "\n"
    "All Rights Reserved.\n"
    "This program comes with absolutely no warranty.\n"
    "See the GNU General Public License, version 3 for details.\n"
    "\n", version, program.c_str());
}
