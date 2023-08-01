
#include "common.h"
#include "libs/utf8/utf8.h"
#include <array>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cctype>

std::filesystem::path utf8_to_path(std::string_view utf8_string) {
#if defined(__cpp_char8_t)
  static_assert(sizeof(char) == sizeof(char8_t));
  return std::filesystem::path(
    reinterpret_cast<const char8_t*>(utf8_string.data()),
    reinterpret_cast<const char8_t*>(utf8_string.data() + utf8_string.size()));
#else
  return std::filesystem::u8path(utf8_string);
#endif
}

std::string path_to_utf8(const std::filesystem::path& path) {
  const auto u8string = path.u8string();
  return std::string(
    reinterpret_cast<const char*>(u8string.data()),
    reinterpret_cast<const char*>(u8string.data() + u8string.size()));
}

extern "C" int siphash(const uint8_t* in, const size_t inlen,
  const uint8_t* k, uint8_t* out, const size_t outlen);

#if defined(USE_ICONV)
# include <iconv.h>
# include <optional>

inline std::optional<std::string> convert_charset_iconv(std::string_view string,
    std::string_view from, std::string_view to) {
  const auto error = ~size_t{ };

  const auto conv = ::iconv_open(std::string(to).c_str(), std::string(from).c_str());
  if (reinterpret_cast<uintptr_t>(conv) == error)
    return std::nullopt;

  auto source_pointer = const_cast<char*>(string.data());
  auto source_size = string.size();
  auto dest = std::string();
  auto dest_buffer = std::vector<char>(1024);
  while (source_size > 0) {
    auto dest_pointer = dest_buffer.data();
    auto dest_size = dest_buffer.size();
    if (::iconv(conv, &source_pointer,
        &source_size, &dest_pointer, &dest_size) == error) {
      if (errno != E2BIG) {
        ++source_pointer;
        --source_size;
      }
    }
    dest.append(dest_buffer.data(), dest_buffer.size() - dest_size);
  }
  ::iconv_close(conv);
  return { std::move(dest) };
}
#endif // USE_ICONV

std::string convert_charset(std::string data, std::string_view from, std::string_view to) {
  if (!iequals(from, to))
    return convert_charset(as_byte_view(data), from, to);
  return data;
}

std::string convert_charset(ByteView data, std::string_view from, std::string_view to) {
  if (!iequals(from, to)) {
#if defined(USE_ICONV)
    if (auto result = convert_charset_iconv(as_string_view(data), from, to))
      return std::move(result.value());
#endif
  }
  return std::string(as_string_view(data));
}

std::string get_hash(ByteView in) {
  auto out = uint64_t{ };
  auto k = std::array<uint8_t, 16>{ };

  siphash(
    reinterpret_cast<const uint8_t*>(in.data()),
    static_cast<size_t>(in.size()), k.data(),
    reinterpret_cast<uint8_t*>(&out), sizeof(out));

  auto ss = std::ostringstream();
  ss << std::setfill('0') << std::setw(16) << std::hex << out;
  return ss.str();
}

std::string format_time(time_t time) {
  // Wed, 21 Oct 2015 07:28:00 GMT
  auto ss = std::ostringstream();
  ss << std::put_time(std::gmtime(&time), "%a, %d %b %Y %H:%M:%S GMT");
  return ss.str();
}

time_t parse_time(const std::string& string) {
  auto ss = std::istringstream(string);
  auto time = tm{ };
  ss >> std::get_time(&time, "%a, %d %b %Y %H:%M:%S GMT");
#if !defined(_WIN32)
  return std::mktime(&time) - timezone;
#else
  return _mkgmtime64(&time);
#endif
}

ByteView as_byte_view(std::string_view data) {
  const auto begin = reinterpret_cast<const std::byte*>(data.data());
  return { begin, static_cast<ByteView::size_type>(data.size()) };
}

std::string_view as_string_view(ByteView data) {
  const auto begin = reinterpret_cast<const char*>(data.data());
  return { begin, static_cast<std::string_view::size_type>(data.size()) };
}

bool iequals(std::string_view s1, std::string_view s2) {
  return s1.size() == s2.size() &&
    std::equal(s1.begin(), s1.end(), s2.begin(),
      [](char a, char b) { return to_lower(a) == to_lower(b); });
}

bool icontains(std::string_view s1, std::string_view s2) {
  return std::search(s1.begin(), s1.end(), s2.begin(), s2.end(),
    [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }) != s1.end();
}

bool is_space(char c) {
  return std::isspace(static_cast<unsigned char>(c));
}

bool is_punct(char c) {
  return std::ispunct(static_cast<unsigned char>(c));
}

char to_lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool starts_with(std::string_view str, std::string_view with) {
  return (str.size() >= with.size() &&
    std::strncmp(str.data(), with.data(), with.size()) == 0);
}

bool ends_with(std::string_view str, std::string_view with) {
  return (str.size() >= with.size() &&
    std::strncmp(str.data() + (str.size() - with.size()),
      with.data(), with.size()) == 0);
}

std::string_view trim(LStringView str) {
  while (!str.empty() && is_space(str.front()))
    str = str.substr(1);
  while (!str.empty() && is_space(str.back()))
    str = str.substr(0, str.size() - 1);
  return str;
}

std::string_view unquote(LStringView str) {
  if (str.size() >= 2 && str.front() == str.back())
    if (str.front() == '"' || str.front() == '\'')
      return str.substr(1, str.size() - 2);
  return str;
}

void replace_all(std::string& data, std::string_view search, std::string_view replace) {
  for (auto pos = data.find(search); pos != std::string::npos;
      pos = data.find(search, pos + replace.size()))
    data.replace(pos, search.size(), replace);
}

std::string to_local_filename(std::string url, size_t max_length) {
  // remove #fragment
  if (auto it = url.find('#'); it != std::string::npos)
    url.resize(it);

  // turn http:// to http/
  if (auto i = url.find("://"); i != std::string::npos)
    url.replace(i, 3, "/");

  // normalize // to /
  for (auto i = url.find("//"); i != std::string::npos; i = url.find("//"))
    url.replace(i, 2, "/");

  // add missing filename
  if (url.back() == '/')
    url += "index";

  // truncate to maximum file, replace rest with hash
  if (url.size() > max_length) {
    auto view = std::string_view(url);
    const auto rest = view.substr(max_length - 17);
    url = url.substr(0, max_length - 17) + "~" + get_hash(as_byte_view(rest));
  }
  return url;
}

template<typename F>
std::string replace_codepoints(const std::string& input, F&& replace) {
  auto output = std::string();
  for (auto it = input.begin(); it != input.end(); )
    if (auto codepoint = replace(Utf8::readCodepoint(it, input.end())))
      Utf8::writeCodepoint(output, codepoint);
  return output;
}

std::string get_legal_filename(const std::string& filename) {
  // see: https://unicode.org/cldr/utility/confusables.jsp
  return replace_codepoints(filename,
    [](uint32_t codepoint) -> uint32_t {
      switch (codepoint) {
        case '/': return 0x2571; // BOX DRAWINGS LIGHT DIAGONAL UPPER RIGHT TO LOWER LEFT
#if 1 || defined(_WIN32)
        case '\\':return 0x2572; // BOX DRAWINGS LIGHT DIAGONAL UPPER LEFT TO LOWER RIGHT
        case '<': return 0x27E8; // MATHEMATICAL LEFT ANGLE BRACKET
        case '>': return 0x27E9; // MATHEMATICAL RIGHT ANGLE BRACKET
        case ':': return 0xA789; // MODIFIER LETTER COLON
        case '"': return 0x02EE; // MODIFIER LETTER DOUBLE APOSTROPHE
        case '|': return 0x2223; // DIVIDES
        case '*': return 0x2217; // ASTERISK OPERATOR
        case '?': return 0xFF1F; // FULLWIDTH QUESTION MARK
#endif
        default: return codepoint;
      }
    });
}

std::string filename_from_url(const std::string& url) {
  auto filename = to_local_filename(url);
  filename = filename.substr(filename.find('/') + 1);
  if (ends_with(filename, "/index"))
    filename.resize(filename.size() - 6);
  return get_legal_filename(filename);
}

std::string url_from_input(std::string_view url_string) {
  if (url_string.find("://") != std::string::npos)
    return std::string(trim(url_string));
  return "http://" + std::string(trim(url_string));
}

StringViewPair split_content_type(std::string_view content_type) {
  auto mime_type = content_type;
  auto charset = std::string_view();

  const auto semicolon = content_type.find(';');
  if (semicolon != std::string_view::npos) {
    mime_type = content_type.substr(0, semicolon);

    auto pos = content_type.find("charset", semicolon);
    if (pos != std::string_view::npos) {
      pos = content_type.find("=", pos);
      if (pos != std::string_view::npos)
        charset = content_type.substr(pos + 1);
    }
  }
  return { trim(mime_type), trim(charset) };
}

std::string get_content_type(std::string_view mime_type, std::string_view charset) {
  return std::string(mime_type) + "; charset=" + std::string(charset);
}

bool is_relative_url(std::string_view url) {
  return get_scheme(url).empty();
}

bool is_same_url(std::string_view a, std::string_view b) {
  if (ends_with(a, "/"))
    a = a.substr(0, a.size() - 1);
  if (ends_with(b, "/"))
    b = b.substr(0, b.size() - 1);
  return (a == b);
}

std::string normalize_path(std::string path) {
  // remove ./ and ../
  replace_all(path, "/./", "/");
  for (;;) {
    const auto it = path.find("/..");
    if (it == std::string::npos)
      break;
    if (it == 0) {
      path.erase(0, 3);
    }
    else {
      const auto slash = path.rfind('/', it - 1);
      path.erase(slash, it - slash + 3);
    }
  }
  return path;
}

std::string to_absolute_url(std::string_view url, const std::string& relative_to) {
  if (!is_relative_url(url))
    return std::string(url);

  const auto base_path_begin = get_scheme_hostname_port(relative_to).size();
  const auto base = relative_to.substr(0, base_path_begin);
  if (starts_with(url, "/")) {
    // try to complete scheme
    if (starts_with(url, "//")) {
      const auto with_scheme = std::string(get_scheme(relative_to)) + ":" + std::string(url);
      if (get_hostname(with_scheme).find('.') != std::string_view::npos)
        return with_scheme;
    }
    return base + std::string(url);
  }
  const auto base_path_end = get_scheme_hostname_port_path(relative_to).size();
  auto path = relative_to.substr(base_path_begin, base_path_end - base_path_begin);

  // remove filename
  if (auto last_slash = path.rfind('/'); last_slash != std::string::npos)
    path.resize(last_slash + 1);
  else
    path += '/';

  path += std::string(url);

  return base + normalize_path(std::move(path));
}

std::string_view to_relative_url(LStringView url, std::string_view base_url) {
  if (starts_with(url, base_url)) {
    if (url == base_url)
      return "/";
    return url.substr(base_url.size());
  }
  return url;
}

std::string_view get_scheme(LStringView url) {
  if (starts_with(url, "http:"))
    return "http";
  if (starts_with(url, "https:"))
    return "https";

  for (auto i = 0u; ; ++i) {
    if (i == url.size())
      return { };
    if (url[i] == ':')
      return url.substr(0, i);
    if (url[i] < 'a' || url[i] > 'z')
      return { };
  }
}

std::string_view get_hostname_port(LStringView url) {
  if (is_relative_url(url))
    return { };

  auto begin = url.find("://");
  if (begin == std::string_view::npos)
    return { };
  begin += 3;

  if (const auto slash = url.find('/', begin); slash != std::string_view::npos)
    return url.substr(begin, slash - begin);
  return url.substr(begin);
}

std::string_view get_hostname(LStringView url) {
  if (is_relative_url(url))
    return { };

  auto hostname_port = get_hostname_port(url);

  if (const auto colon = hostname_port.find(":"); colon != std::string_view::npos)
    return hostname_port.substr(0, colon);
  return hostname_port;
}

std::string_view get_scheme_hostname_port(LStringView url) {
  if (is_relative_url(url))
    return url;

  auto begin = url.find("://");
  if (begin == std::string_view::npos)
    return { };
  begin += 3;

  if (const auto slash = url.find('/', begin); slash != std::string_view::npos)
    return url.substr(0, slash);
  return url;
}

std::string_view get_scheme_hostname_port_path(LStringView url) {
  auto n = std::string_view::npos;
  const auto question = url.find("?");
  const auto hash = url.find("#");
  if (question != std::string_view::npos) {
    if (hash != std::string_view::npos && hash < question)
      n = hash;
    else
      n = question;
  }
  else if (hash != std::string_view::npos) {
    n = hash;
  }
  return url.substr(0, n);
}

std::string_view get_scheme_hostname_port_path_query(LStringView url) {
  auto n = std::string_view::npos;
  const auto hash = url.find("#");
  if (hash != std::string_view::npos)
    n = hash;
  return url.substr(0, n);
}

std::string_view get_without_first_domain(LStringView url) {
  if (const auto dot = url.find('.'); dot != std::string_view::npos)
    return url.substr(dot + 1);
  return { };
}

std::string_view get_file_extension(LStringView url) {
  auto path = get_scheme_hostname_port_path(url);
  auto dot = path.rfind('.');
  auto slash = path.rfind('/');
  if (dot != std::string_view::npos && dot > slash)
    if (path.size() - dot <= 5)
      return path.substr(dot + 1);
  return { };
}

std::string_view unpatch_url(LStringView url) {
  if (is_relative_url(url)) {
    const auto scheme = get_scheme(url.substr(1));
    if (scheme == "http" || scheme == "https")
      return url.substr(1);
  }
  return url;
}

std::string get_identifying_url(std::string url, ByteView request_data) {
  if (!request_data.empty()) {
    const auto delim = (url.find('?') != std::string::npos ? '&' : '?');
    url = url + delim + get_hash(request_data);
  }
  return url;
}

std::string url_to_regex(std::string_view url, bool sub_domains) {
  auto regex = std::string(url);
  replace_all(regex, "http://", "https?://");
  if (sub_domains)
    replace_all(regex, "://", "://([^/]+.)?");
  replace_all(regex, ".", "\\.");
  replace_all(regex, "/", "\\/");
  return "^" + regex + ".*";
}

std::string generate_id(int length) {
  auto rand = std::random_device();
  auto generator = std::mt19937(rand());
  auto half = std::uniform_int_distribution<>(0, 127);
  auto full = std::uniform_int_distribution<>(0, 255);
  auto ss = std::stringstream();
  for (auto i = 0; i < length; ++i)
    ss << std::hex << std::setfill('0') << std::setw(2) <<
      (i == 0 ? half(generator) : full(generator));
  return ss.str();
}

std::filesystem::path generate_temporary_filename(std::string filename) {
  auto rand = std::random_device();
  for (auto i = 0; i < 10; i++)
    filename.push_back(static_cast<char>('0' + rand() % 10));
  filename += ".tmp";
  return std::filesystem::temp_directory_path() / filename;
}
