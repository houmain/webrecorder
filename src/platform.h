#pragma once

#include "common.h"

enum class Event {
  fatal,
  error,
  info,
  accept,
  redirect,
  download_started,
  download_spared,
  download_finished,
  download_failed,
  download_blocked,
  writing_failed,
};

std::string get_message_utf8(std::error_code error);
std::string get_message_utf8(const std::exception& ex);

std::unique_ptr<void, void(*)(void*)> begin_output_line(bool to_stderr);
void write_output(int value);
void write_output(const char* utf8);
void write_output(const std::string& utf8);
void write_output(Event event);

template<typename... Args>
void log(Event event, Args&&... args) {
  auto guard = begin_output_line(event == Event::fatal);
  write_output(event);
  write_output(" ");
  (write_output(args), ...);
}

std::string convert_charset(std::string data, std::string_view from, std::string_view to);
std::string convert_charset(ByteView data, std::string_view from, std::string_view to);
void open_browser(const std::string& url);

extern int run(int argc, const char* argv[]) noexcept;
