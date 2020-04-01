
#include "Settings.h"
#include "HtmlPatcher.h"
#include <regex>

bool interpret_commandline(Settings& settings, int argc, const char* argv[]) {
  for (auto i = 1; i < argc; i++) {
    const auto argument = std::string_view(argv[i]);
    if (argument == "-i") {
      if (++i >= argc)
        return false;
      settings.url = url_from_input(std::string(unquote(argv[i])));
    }
    else if (argument == "-o") {
      if (++i >= argc)
        return false;
      settings.filename = std::filesystem::u8path(unquote(argv[i]));
    }
    else if (argument == "-bl") {
      if (++i >= argc)
        return false;
      settings.blocked_hosts_lists.push_back(
        std::filesystem::u8path(unquote(argv[i])));
    }
    else if (argument == "-by") {
      if (++i >= argc)
        return false;
      settings.bypassed_hosts_lists.push_back(
        std::filesystem::u8path(unquote(argv[i])));
    }
    else if (argument == "-p") {
      if (++i >= argc)
        return false;
      settings.proxy_server = unquote(argv[i]);
    }
    else if (argument == "-f") {
      if (++i >= argc || std::strlen(argv[i]) != 1)
        return false;
      switch (argv[i][0]) {
        case 'N':
          settings.follow_link_policy = FollowLinkPolicy::none;
          break;
        case 'H':
          settings.follow_link_policy = FollowLinkPolicy::same_hostname;
          break;
        case 'D':
          settings.follow_link_policy = FollowLinkPolicy::same_second_level_domain;
          break;
        case 'P': 
          settings.follow_link_policy = FollowLinkPolicy::same_path;
          break;
        case 'A':
          settings.follow_link_policy = FollowLinkPolicy::all;
          break;
        default:
          return false;
      }
    }
    else if (argument == "-v") {
      if (++i >= argc || std::strlen(argv[i]) != 1)
        return false;
      switch (argv[i][0]) {
        case 'N':
          settings.validation_policy = ValidationPolicy::never;
          break;
        case 'E':
          settings.validation_policy = ValidationPolicy::when_expired;
          break;
        case 'R':
          settings.validation_policy = ValidationPolicy::when_expired_reload;
          break;
        case 'A':
          settings.validation_policy = ValidationPolicy::always;
          break;
        default:
          return false;
      }
    }
    else if (argument == "-e") {
      if (++i >= argc)
        return false;
      for (auto c = argv[i]; *c; ++c) {
        switch (*c) {
          case 'T': settings.filename_from_title = true; break;
          case 'L': settings.allow_lossy_compression = true; break;
          default:
            return false;
        }
      }
    }
    else if (argument == "-d") {
      if (++i >= argc)
        return false;
      for (auto c = argv[i]; *c; ++c) {
        switch (*c) {
          case 'R': settings.read = false; break;
          case 'W': settings.write = false; break;
          case 'A': settings.append = false; break;
          case 'D': settings.download = false; break;
          case 'B': settings.open_browser = false; break;
          default:
            return false;
        }
      }
    }
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
        settings.filename = filename;

      if (!settings.url.empty() && settings.filename.empty())
        settings.filename = std::filesystem::u8path(filename_from_url(settings.url));
    }
    else {
      return false;
    }
  }

  if (settings.filename.empty())
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
    "Usage: %s [-options] [URL|FILE]\n"
    "  -i <URL>    set initial request URL.\n"
    "  -o <FILE>   set read/write filename.\n"
    "  -f <POLICY> follow link policy:\n"
    "                N  none (default)\n"
    "                H  same hostname\n"
    "                D  same second-level domain\n"
    "                P  same path\n"
    "                A  all\n"
    "  -v <POLICY> validation policy:\n"
    "                N  never (default)\n"
    "                E  when expired\n"
    "                R  when expired on reload\n"
    "                A  always\n"
    "  -bl <FILE>  add host block list.\n"
    "  -by <FILE>  add host bypass list.\n"
    "  -p <PROXY>  set a HTTP proxy (host:port).\n"
    "  -d <FLAGS>  disable (enabled by default):\n"
    "                W  writing to file\n"
    "                R  reading from file\n"
    "                A  appending\n"
    "                D  downloading\n"
    "                B  opening of browser\n"
    "  -e <FLAGS>  enable (disabled by default):\n"
    "                T  generate filename from title\n"
    "                L  allow lossy compression\n"
    "  -h, --help  print this help.\n"
    "\n"
    "All Rights Reserved.\n"
    "This program comes with absolutely no warranty.\n"
    "See the GNU General Public License, version 3 for details.\n"
    "\n", version, program.c_str());
}
