
#include "platform.h"
#include <mutex>

#if __has_include(<iconv.h>)
# define USE_ICONV
#endif

#if defined(USE_ICONV)
# include <iconv.h>

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
#endif

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

void write_output(Event event) {
  write_output([&]() {
    switch (event) {
      case Event::fatal: return "FATAL";
      case Event::error: return "ERROR";
      case Event::info: return "INFO";
      case Event::accept: return "ACCEPT";
      case Event::redirect: return "REDIRECT";
      case Event::download_started: return "DOWNLOAD";
      case Event::download_omitted: return "DOWNLOAD_OMITTED";
      case Event::download_finished: return "DOWNLOAD_FINISHED";
      case Event::download_failed: return "DOWNLOAD_FAILED";
      case Event::download_blocked: return "DOWNLOAD_BLOCKED";
      case Event::writing_failed: return "WRITING_FAILED";
    }
    return "";
  }());
}

#if !defined(_WIN32)

namespace {
  std::mutex g_output_mutex;
  FILE* g_current_output_file;

  void end_output_line(void*) {
    std::fputc('\n', g_current_output_file);
    std::fflush(g_current_output_file);
    g_current_output_file = nullptr;
    g_output_mutex.unlock();
  }
} // namespace

std::string get_message_utf8(std::error_code error) {
  if (error.value() == 2) // End of File
    return { };
  return error.message();
}

std::string get_message_utf8(const std::exception& ex) {
  return ex.what();
}

std::unique_ptr<void, void(*)(void*)> begin_output_line(bool to_stderr) {
  g_output_mutex.lock();
  g_current_output_file = (to_stderr ? stderr : stdout);
  return { reinterpret_cast<void*>(1), &end_output_line };
}

void write_output(int value) {
  std::fprintf(g_current_output_file, "%i", value);
}

void write_output(const char* utf8) {
  for (auto c = utf8; *c; ++c) {
    if (*c == '\n') {
      std::fputc('\\', g_current_output_file);
      std::fputc('n', g_current_output_file);
    }
    else if (*c == '\r') {
      std::fputc('\\', g_current_output_file);
      std::fputc('r', g_current_output_file);
    }
    else {
      std::fputc(*c, g_current_output_file);
    }
  }
}

void write_output(const std::string& utf8) {
  write_output(utf8.c_str());
}

void open_browser(const std::string& url) {
  if (std::system(("xdg-open \"" + url + "\"").c_str()))
    std::system(("open \"" + url + "\"").c_str());
}

int main(int argc, const char* argv[]) {
  return run(argc, argv);
}

#else // _WIN32
# define WIN32_LEAN_AND_MEAN
# define NOMINMAX
# include <Windows.h>
# include <shellapi.h>
# include <csignal>

namespace {
  std::mutex g_output_mutex;
  FILE* g_current_output_file;

  void end_output_line(void*) {
    std::fputwc(L'\n', g_current_output_file);
    std::fflush(g_current_output_file);
    g_current_output_file = nullptr;
    g_output_mutex.unlock();
  }

  void write_output_wide(std::wstring_view str) {
    for (auto c : str) {
      if (c == L'\n') {
        std::fputwc(L'\\', g_current_output_file);
        std::fputwc(L'n', g_current_output_file);
      }
      else if (c == L'\r') {
        std::fputwc(L'\\', g_current_output_file);
        std::fputwc(L'r', g_current_output_file);
      }
      else {
        std::fputwc(c, g_current_output_file);
      }
    }
  }

  std::wstring multibyte_to_wide(std::string_view str) {
    auto result = std::wstring();
    result.resize(MultiByteToWideChar(CP_ACP, 0, 
      str.data(), static_cast<int>(str.size()),
      NULL, 0));
    MultiByteToWideChar(CP_ACP, 0, 
      str.data(), static_cast<int>(str.size()), 
      result.data(), static_cast<int>(result.size()));
    return result;
  }

  std::string wide_to_utf8(std::wstring_view str) {
    auto result = std::string();
    result.resize(WideCharToMultiByte(CP_UTF8, 0, str.data(), 
      static_cast<int>(str.size()), NULL, 0, NULL, 0));
    WideCharToMultiByte(CP_UTF8, 0, 
      str.data(), static_cast<int>(str.size()),
      result.data(), static_cast<int>(result.size()),
      NULL, 0);
    return result;
  }

  std::wstring utf8_to_wide(std::string_view str) {
    auto result = std::wstring();
    result.resize(MultiByteToWideChar(CP_UTF8, 0,
      str.data(), static_cast<int>(str.size()),
      NULL, 0));
    MultiByteToWideChar(CP_UTF8, 0,
      str.data(), static_cast<int>(str.size()),
      result.data(), static_cast<int>(result.size()));
    return result;
  }
} // namespace

std::string get_message_utf8(std::error_code error) {
  return wide_to_utf8(multibyte_to_wide(error.message()));
}

std::string get_message_utf8(const std::exception& ex) {
  return wide_to_utf8(multibyte_to_wide(ex.what()));
}

std::unique_ptr<void, void(*)(void*)> begin_output_line(bool to_stderr) {
  g_output_mutex.lock();
  g_current_output_file = (to_stderr ? stderr : stdout);
  return { reinterpret_cast<void*>(1), &end_output_line };
}

void write_output(int value) {
  std::fprintf(g_current_output_file, "%i", value);
}

void write_output(const char* utf8) {
  write_output_wide(utf8_to_wide(utf8));
}

void write_output(const std::string& utf8) {
  write_output_wide(utf8_to_wide(utf8));
}

void open_browser(const std::string& url) {
  ShellExecuteW(0, L"open", utf8_to_wide(url).c_str(),
    NULL, NULL, SW_SHOWNORMAL);
}

int wmain(int argc, wchar_t* wargv[]) {
  auto argv_strings = std::vector<std::string>();
  for (auto i = 0; i < argc; ++i)
    argv_strings.push_back(wide_to_utf8(wargv[i]));
  auto argv = std::vector<const char*>();
  for (const auto& string : argv_strings)
    argv.push_back(string.c_str());

  SetConsoleCtrlHandler([](DWORD) {
    std::raise(SIGINT);
    // wait until it properly shut down
    for (;; Sleep(100));
    return TRUE;
  }, TRUE);

  return run(argc, argv.data());
}

#endif // _WIN32
