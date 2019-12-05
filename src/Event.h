#pragma once

#include <iostream>
#include <mutex>

enum class Event {
  fatal,
  error,
  accept,
  download_started,
  download_finished,
  download_failed,
  download_blocked,
  writing_failed,
};

inline std::mutex g_cout_mutex;

template<typename... Args>
void log(Event event, Args&&... args) {
  auto type = [&]() -> const char* {
    switch (event) {
      case Event::fatal: return "FATAL";
      case Event::error: return "ERROR";
      case Event::accept: return "ACCEPT";
      case Event::download_started: return "DOWNLOAD";
      case Event::download_finished: return "DOWNLOAD_FINISHED";
      case Event::download_failed: return "DOWNLOAD_FAILED";
      case Event::download_blocked: return "DOWNLOAD_BLOCKED";
      case Event::writing_failed: return "WRITING_FAILED";
    }
    return nullptr;
  }();
  if (!type)
    return;

  auto lock = std::lock_guard(g_cout_mutex);
  std::cout << type << " ";
  (std::cout << ... << args);
  std::cout << std::endl;
}
