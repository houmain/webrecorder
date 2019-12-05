
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
    else if (argument == "-b") {
      if (++i >= argc)
        return false;
      settings.host_block_lists.push_back(std::filesystem::u8path(unquote(argv[i])));
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
        case 'A': 
          settings.follow_link_policy = FollowLinkPolicy::all;
          break;
        case 'T': 
          settings.follow_link_policy = FollowLinkPolicy::same_top_level_domain;
          break;
        case 'H': 
          settings.follow_link_policy = FollowLinkPolicy::same_hostname;
          break;
        case 'P': 
          settings.follow_link_policy = FollowLinkPolicy::same_subpath;
          break;
        case 'N': 
          settings.follow_link_policy = FollowLinkPolicy::none;
          break;
        default:
          return false;
      }
    }
    else if (argument == "-v") {
      if (++i >= argc || std::strlen(argv[i]) != 1)
        return false;
      switch (argv[i][0]) {
        case 'A':
          settings.validation_policy = ValidationPolicy::always;
          break;
        case 'E':
          settings.validation_policy = ValidationPolicy::when_expired;
          break;
        case 'R':
          settings.validation_policy = ValidationPolicy::when_expired_reload;
          break;
        case 'N':
          settings.validation_policy = ValidationPolicy::never;
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
  return true;
}

void print_help_message(std::ostream& os, const char* argv0) {
  auto program = std::string(argv0);
  if (auto i = program.rfind('/'); i != std::string::npos)
    program = program.substr(i + 1);
  if (auto i = program.rfind('.'); i != std::string::npos)
    program = program.substr(0, i);

  os << program
    << " " <<
#include "_version.h"
    << " (c) 2019 by Albert Kalchmair\n"
    << "\n"
    << "Usage: " << program << " [-options] [URL|FILE]\n"
      "  -i <URL>    set initial request URL.\n"
      "  -o <FILE>   set read/write filename.\n"
      "  -f <POLICY> follow link policy:\n"
      "                A  all\n"
      "                T  same top level domain\n"
      "                H  same hostname\n"
      "                P  same subpath (default)\n"
      "                N  none\n"
      "  -v <POLICY> validation policy:\n"
      "                A  always\n"
      "                E  when expired\n"
      "                R  when expired on reload (default)\n"
      "                N  never\n"
      "  -b <FILE>   add host block list.\n"
      "  -p <PROXY>  set a HTTP proxy (host:port).\n"
      "  -d <FLAGS>  disable (enabled by default):"
      "                W writing to file\n"
      "                R reading from file\n"
      "                A appending\n"
      "                D downloading\n"
      "                B opening of browser\n"
      "  -e <FLAGS>  enable (disabled by default):"
      "                T generate filename from title\n"
      "  -h, --help print this help.\n"
      "\n"
      "All Rights Reserved.\n"
      "This program comes with absolutely no warranty.\n"
      "See the GNU General Public License, version 3 for details.\n"
    << std::endl;
}
