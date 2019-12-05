#pragma once

#include "Server.h"
#include "Client.h"
#include "HeaderStore.h"
#include "CookieStore.h"
#include "Archive.h"
#include "Settings.h"
#include <regex>

struct Settings;
class HostBlocker;

struct CacheInfo {
  bool expired;
  std::time_t last_modified_time;
  std::string etag;
};

class Logic final {
public:
  Logic(Settings* settings, std::unique_ptr<HostBlocker> host_blocker);
  Logic(const Logic&) = delete;
  Logic& operator=(const Logic&) = delete;
  ~Logic();

  void setup(std::string local_server_url,
             std::function<void()> start_threads_callback);
  void handle_request(Server::Request request);
  void handle_error(Server::Request request, std::error_code error);

private:
  void set_server_base(const std::string& url);
  std::optional<CacheInfo> get_cache_info(const Server::Request& request, 
    const std::string& url);
  bool serve_from_archive(Server::Request& request, const std::string& url,
    bool write_to_archive);
  void forward_request(Server::Request request, const std::string& url,
    const std::optional<CacheInfo>& cache_info);
  void handle_response(Server::Request& request,
    const std::string& url, Client::Response response);
  void serve_error(Server::Request& request, const std::string& url,
    StatusCode status_code);
  void serve_blocked(Server::Request& request, const std::string& url);
  void serve_file(Server::Request& request, const std::string& url,
    StatusCode status_code, const Header& header, ByteView data, time_t response_time);
  void async_write_file(const std::string& identifying_url,
    StatusCode status_code, const Header& header, ByteView data,
    time_t response_time, std::function<void(bool)>&& on_complete);
  void set_filename_from_title(const std::string& title);
  void set_strict_transport_security(const std::string& url, bool include_subdomains);
  std::string apply_strict_transport_security(std::string url) const;

  // immutable
  Settings& m_settings;
  std::string m_follow_link_regex;
  std::unique_ptr<ArchiveReader> m_archive_reader;
  std::unique_ptr<HostBlocker> m_host_blocker;
  HeaderStore m_header_reader;

  // only updated on main thread
  std::function<void()> m_start_threads_callback;
  std::string m_local_server_base;
  std::string m_server_base;
  std::string m_server_base_path;

  // threadsafe
  Client m_client;
  CookieStore m_cookie_store;

  // modifications sequenced by mutex
  mutable std::mutex m_write_mutex;
  std::unique_ptr<ArchiveWriter> m_archive_writer;
  HeaderStore m_header_writer;
  std::map<std::string, std::regex, std::less<void>> m_strict_transport_security;
  bool m_filename_from_title_set{ };
};
