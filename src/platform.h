#pragma once

#include "common.h"
#include <filesystem>

enum class Event {
  fatal,
  error,
  info,
  accept,
  redirect,
  download_started,
  download_omitted,
  download_finished,
  download_failed,
  download_blocked,
  served,
  writing_failed,
};

std::string read_utf8_textfile(const std::filesystem::path& filename);
std::string get_message_utf8(std::error_code error);
std::string get_message_utf8(const std::exception& ex);

std::unique_ptr<void, void(*)(void*)> begin_output_line(bool to_stderr);
void write_output(int value);
void write_output(const char* utf8);
void write_output(const std::string& utf8);
void write_output(Event event);
template <typename T>
void write_output(T value) { write_output(static_cast<int>(value)); }

template <typename... Args>
void log(Event event, Args&&... args) {
  auto guard = begin_output_line(event == Event::fatal);
  write_output(event);
  write_output(" ");
  (write_output(args), ...);
}

void open_browser(const std::string& url);

extern int run(int argc, const char* argv[]) noexcept;
