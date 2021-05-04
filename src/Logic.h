#pragma once

#include "Server.h"
#include "Client.h"
#include "HeaderStore.h"
#include "CookieStore.h"
#include "Archive.h"
#include "Settings.h"
#include "CacheInfo.h"
#include <regex>

struct Settings;
class HostList;

class Logic final {
public:
  explicit Logic(Settings* settings);
  Logic(const Logic&) = delete;
  Logic& operator=(const Logic&) = delete;
  ~Logic();

  // only call on main thread
  void set_local_server_url(std::string local_server_url);
  void set_start_threads_callback(std::function<void()> callback);

  // threadsafe
  void handle_request(Server::Request request);
  void handle_error(Server::Request request, std::error_code error);

private:
  void initialize();
  void finish();
  void set_server_base(const std::string& url);
  void send_cors_response(Server::Request request);
  [[nodiscard]] std::string apply_strict_transport_security(std::string url) const;
  void serve_blocked(Server::Request& request, const std::string& url);
  void serve_error(Server::Request& request, const std::string& url,
    StatusCode status_code);
  void handle_file_request(Server::Request request, const std::string& url);
  void forward_request(Server::Request request, const std::string& url,
    const std::optional<CacheInfo>& cache_info);
  void handle_response(Server::Request& request,
    const std::string& url, Client::Response response);
  [[nodiscard]] bool serve_previously_served(Server::Request& request, const std::string& url);
  [[nodiscard]] bool serve_from_archive(Server::Request& request, const std::string& url,
    bool write_to_archive);
  void serve_file(Server::Request& request, const std::string& url,
    StatusCode status_code, const Header& header, ByteView data, time_t response_time);
  void handle_initial_redirects(const std::string& url,
    const StatusCode status_code, const Header& header);
  void set_strict_transport_security(const std::string& url, bool include_subdomains);
  void async_write_file(const std::string& identifying_url,
    StatusCode status_code, const Header& header, ByteView data,
    time_t response_time, bool allow_lossy_compression,
    std::function<void(bool)>&& on_complete);
  void append_unrequested_files();

  // only updated while single threaded
  Settings& m_settings;
  std::string m_uid;
  std::unique_ptr<ArchiveReader> m_archive_reader;
  std::unique_ptr<HostList> m_blocked_hosts;
  HeaderStore m_header_reader;
  std::string m_inject_javascript_code;
  std::string m_local_server_base;
  std::string m_server_base;
  std::string m_server_base_path;
  std::function<void()> m_start_threads_callback;

  // threadsafe
  Client m_client;
  CookieStore m_cookie_store;

  // modifications sequenced by mutex
  mutable std::mutex m_write_mutex;
  std::unique_ptr<ArchiveWriter> m_archive_writer;
  HeaderStore m_header_writer;
  std::map<std::string, std::regex, std::less<void>> m_strict_transport_security;
};

struct FileRequestAction {
  bool serve;
  bool write;
  bool download;
};

FileRequestAction get_file_request_action(
  const Settings& settings, bool archived,  bool expired);
