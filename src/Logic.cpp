
#include "Logic.h"
#include "HtmlPatcher.h"
#include "HostList.h"
#include "LossyCompressor.h"
#include "platform.h"
#include <sstream>
#include <cassert>

namespace {
  const auto basic_text_header = Header{ { "Content-Type", "text/javascript;charset=utf-8" } };
  const auto set_cookie_request = "/__webrecorder_setcookie";
  const auto shutdown_request = "/__webrecorder_exit";
  const auto inject_javascript_request = "/__webrecorder.js";
  const auto first_overlay_path = "first/";
} // namespace

Logic::Logic(Settings* settings)
  : m_settings(*settings),
    m_client(m_settings.proxy_server) {
  initialize();
}

Logic::~Logic() {
  finish();
}

void Logic::initialize() {
  m_settings.input_file = utf8_to_path(
    get_legal_filename(path_to_utf8(m_settings.input_file)));
  m_settings.output_file = utf8_to_path(
    get_legal_filename(path_to_utf8(m_settings.output_file)));

  if (!m_settings.input_file.empty()) {
    auto archive_reader = std::make_unique<ArchiveReader>();
    if (archive_reader->open(m_settings.input_file))
      m_archive_reader = std::move(archive_reader);
  }

  if (m_archive_reader) {
    if (m_settings.url.empty())
      if (auto data = m_archive_reader->read("url"); !data.empty())
        m_settings.url = std::string(as_string_view(data));

    if (auto data = m_archive_reader->read("uid"); !data.empty())
      m_uid = std::string(as_string_view(data));

    if (auto data = m_archive_reader->read("headers"); !data.empty())
      m_header_reader.deserialize(as_string_view(data));

    if (auto data = m_archive_reader->read("cookies"); !data.empty())
      m_cookie_store.deserialize(as_string_view(data));

    if (m_settings.serve_policy == ServePolicy::first_archived)
      m_archive_reader->set_overlay_path(first_overlay_path);
  }
  if (m_uid.empty())
    m_uid = generate_id();

  if (m_settings.url.empty())
    throw std::runtime_error(m_settings.input_file.empty() ?
      "no URL specified" : "reading file failed");

  if (!m_settings.output_file.empty()) {
    m_archive_writer = std::make_unique<ArchiveWriter>();
    for (auto i = 0; i < 5; ++i)
      if (m_archive_writer->open(generate_temporary_filename("webrecorder-")))
        break;
    if (!m_archive_writer->is_open())
      throw std::runtime_error("opening temporary file failed");

    m_archive_writer->move_on_close(m_settings.output_file, true);
    m_archive_writer->write("uid", as_byte_view(m_uid));
    m_archive_writer->write("url", as_byte_view(m_settings.url));

    if (m_settings.allow_lossy_compression)
      m_archive_writer->set_lossy_compressor(
        std::make_unique<LossyCompressor>());
  }

  if (m_settings.url.empty())
    throw std::runtime_error(m_settings.input_file.empty() ?
      "no URL specified" : "reading file failed");

  auto blocked_hosts = std::make_unique<HostList>();
  for (const auto& file : m_settings.block_hosts_files)
    blocked_hosts->add_hosts_from_file(file);
  if (blocked_hosts->has_hosts())
    m_blocked_hosts = std::move(blocked_hosts);

  if (!m_settings.inject_javascript_file.empty())
    m_inject_javascript_code = read_utf8_textfile(m_settings.inject_javascript_file);

  set_server_base(m_settings.url);
}

void Logic::finish() {
  append_unrequested_files();

  m_archive_reader.reset();

  if (m_archive_writer) {
    m_archive_writer->write("headers", as_byte_view(m_header_writer.serialize()));
    m_archive_writer->write("cookies", as_byte_view(m_cookie_store.serialize()));
    m_archive_writer.reset();
  }
}

void Logic::set_local_server_url(std::string local_server_url) {
  m_local_server_base = get_scheme_hostname_port(local_server_url);
  log(Event::accept, local_server_url);
}

void Logic::set_start_threads_callback(std::function<void()> callback) {
  m_start_threads_callback = std::move(callback);
}

void Logic::set_server_base(const std::string& url) {
  m_server_base = get_scheme_hostname_port(url);
  m_server_base_path = get_scheme_hostname_port_path(url);
}

void Logic::handle_request(Server::Request request) {
  if (ends_with(request.path(), shutdown_request)) {
    request.send_response(StatusCode::success_no_content, { }, { });
    return Client::shutdown();
  }

  if (request.method() == "OPTIONS")
    return send_cors_response(std::move(request));

  auto url = to_absolute_url(unpatch_url(request.path()), m_server_base);
  if (!request.query().empty())
    url += "?" + request.query();

  if (get_scheme(url) == "http")
    url = apply_strict_transport_security(std::move(url));

  if (ends_with(request.path(), inject_javascript_request))
    return request.send_response(StatusCode::success_ok,
      basic_text_header, as_byte_view(m_inject_javascript_code));

  if (ends_with(request.path(), set_cookie_request)) {
    m_cookie_store.set(url, as_string_view(request.data()));
    return request.send_response(StatusCode::success_no_content, {
      { "Access-Control-Allow-Origin", "*" }
     }, { });
  }

  if (m_blocked_hosts && m_blocked_hosts->contains(url))
    return serve_blocked(request, url);

  handle_file_request(std::move(request), url);
}

void Logic::send_cors_response(Server::Request request) {
  auto response_header = Header();
  response_header.emplace("Cache-Control", "no-store");
  response_header.emplace("Access-Control-Max-Age", "-1");
  for (const auto& [name, value] : request.header())
    if (iequals(name, "Origin")) {
      response_header.emplace("Access-Control-Allow-Origin", m_local_server_base);
      response_header.emplace("Access-Control-Allow-Credentials", "true");
    }
    else if (iequals(name, "Access-Control-Request-Method")) {
      response_header.emplace("Access-Control-Allow-Method", value);
    }
    else if (iequals(name, "Access-Control-Request-Headers")) {
      response_header.emplace("Access-Control-Allow-Headers", value);
    }
  request.send_response(StatusCode::success_no_content, response_header, { });
}

std::string Logic::apply_strict_transport_security(std::string url) const {
  auto lock = std::lock_guard(m_write_mutex);
  for (const auto& [_, regex] : m_strict_transport_security)
    if (std::regex_match(url, regex)) {
      url.insert(4, "s");
      break;
    }
  return url;
}

void Logic::serve_blocked(Server::Request& request, const std::string& url) {
  log(Event::download_blocked, url);
  serve_error(request, url, StatusCode::client_error_not_found);
}

void Logic::serve_error(Server::Request& request, const std::string& url,
                        StatusCode status_code) {
  if (status_code == StatusCode::unknown)
    status_code = StatusCode::client_error_not_found;
  log(Event::download_failed, url);
  request.send_response(status_code, basic_text_header, { });
}

void Logic::handle_file_request(Server::Request request, const std::string& url) {
  if (serve_previously_served(request, url))
    return;

  const auto identifying_url = get_identifying_url(url, request.data());
  const auto entry = m_header_reader.read(identifying_url);
  const auto archived = (entry != nullptr);
  const auto cache_info = (!archived ? std::nullopt :
    get_cache_info(entry->status_code, entry->header, request.header()));
  const auto expired = (!cache_info.has_value() || cache_info->expired);

  const auto action = get_file_request_action(m_settings, archived, expired);
  if (action.serve && !serve_from_archive(request, url, action.write))
    log(Event::error);

  if (!action.download) {
    if (!request.response_sent())
      serve_error(request, url, StatusCode::client_error_not_found);
    return;
  }
  forward_request(std::move(request), url, cache_info);
}

void Logic::forward_request(Server::Request request, const std::string& url,
    const std::optional<CacheInfo>& cache_info) {

  log(Event::download_started, url);

  auto header = Header();
  for (const auto& [name, value] : request.header())
    if (iequals(name, "Origin")) {
      header.emplace(name, m_server_base);
    }
    else if (!iequals_any(name, "Host", "Accept-Encoding", "Referer")) {
      header.emplace(name, value);
    }

  header.emplace("Referer", get_scheme_hostname_port(url));

  if (auto cookies = m_cookie_store.get_cookies_list(url); !cookies.empty())
    header.emplace("Cookie", cookies);

  if (cache_info && cache_info->last_modified_time)
    header.emplace("If-Modified-Since", format_time(cache_info->last_modified_time));
  if (cache_info && !cache_info->etag.empty())
    header.emplace("If-None-Match", cache_info->etag);

  const auto& data = request.data();
  const auto& method = request.method();
  const auto& timeout = (cache_info ?
    m_settings.refresh_timeout : m_settings.request_timeout);
  m_client.request(url, method, std::move(header), data, timeout,
    [ this, url,
      request = std::make_shared<Server::Request>(std::move(request))
    ](Client::Response response) {
      handle_response(*request, url, std::move(response));
    });
}

void Logic::handle_response(Server::Request& request,
    const std::string& url, Client::Response response) {

  const auto status_code = response.status_code();
  if (!is_success(status_code) && !is_redirect(status_code))
    if (serve_from_archive(request, url, true))
      return log(Event::download_omitted, url);

  if (response.error())
    return serve_error(request, url, status_code);

  log(Event::download_finished, status_code, " ", response.data().size(), " ", url);
  const auto response_time = std::time(nullptr);

  serve_file(request, url, status_code,
    response.header(), response.data(), response_time);

  const auto& header = response.header();
  const auto& data = response.data();
  async_write_file(get_identifying_url(url, request.data()),
    status_code, header, data, response_time, true,
    [response = std::make_shared<Client::Response>(std::move(response))
    ](bool succeeded) {
      if (!succeeded)
        log(Event::writing_failed);
    });
}

void Logic::handle_error(Server::Request, std::error_code error) {
  if (const auto message = get_message_utf8(error); !message.empty())
    log(Event::error, message);
}

bool Logic::serve_previously_served(Server::Request& request, const std::string& url) {
  auto lock = std::lock_guard(m_write_mutex);
  const auto identifying_url = get_identifying_url(url, request.data());
  const auto filename = to_local_filename(identifying_url);
  if (m_archive_writer && m_archive_writer->contains(filename))
    if (auto entry = m_header_writer.read(identifying_url)) {
      m_archive_writer->async_read(filename,
        [this, url, entry,
         request = std::make_shared<Server::Request>(std::move(request))
        ](ByteVector data, time_t modification_time) mutable {
          serve_file(*request, url, entry->status_code,
            entry->header, data, modification_time);
        });
      return true;
    }
  return false;
}

bool Logic::serve_from_archive(Server::Request& request,
    const std::string& url, bool write_to_archive) {
  if (!m_archive_reader)
    return false;
  const auto identifying_url = get_identifying_url(url, request.data());
  const auto entry = m_header_reader.read(identifying_url);
  if (!entry)
    return false;

  const auto filename = to_local_filename(identifying_url);
  const auto info = m_archive_reader->get_file_info(filename);
  const auto response_time = (info.has_value() ? info->modification_time : std::time(nullptr));
  auto data = m_archive_reader->read(filename);
  serve_file(request, url, entry->status_code, entry->header, data, response_time);

  if (write_to_archive) {
    auto data_view = ByteView(data);
    async_write_file(identifying_url,
      entry->status_code, entry->header,
      data_view, response_time, false,
      [data = std::move(data)](bool succeeded) {
        if (!succeeded)
          log(Event::writing_failed);
      });
  }
  return true;
}

void Logic::serve_file(Server::Request& request, const std::string& url,
    const StatusCode status_code, const Header& header, ByteView data,
    time_t response_time) {

  if (request.response_sent())
    return;

  handle_initial_redirects(url, status_code, header);

  auto content_type = std::string();
  if (auto it = header.find("Content-Type"); it != header.end())
    content_type = it->second;
  const auto [mime_type, charset] = split_content_type(content_type);

  // cookies are stored by webrecorder and accessible using JavaScript
  auto [cookie_begin, cookie_end] = header.equal_range("Set-Cookie");
  for (auto it = cookie_begin; it != cookie_end; ++it)
    m_cookie_store.set(url, it->second);

  auto patched_data = std::optional<std::string>();
  if (!data.empty() && iequals_any(mime_type, "text/html")) {
    const auto patcher = HtmlPatcher(
      m_server_base, url,
      convert_charset(data, (charset.empty() ? "utf-8" : charset), "utf-8"),
      (m_inject_javascript_code.empty() ? "" : inject_javascript_request),
      m_settings.patch_base_tag,
      m_settings.patch_title,
      m_cookie_store.get_cookies_list(url),
      response_time);

    patched_data.emplace(
      convert_charset(patcher.get_patched(), "utf-8", (charset.empty() ? "utf-8" : charset)));
    data = as_byte_view(patched_data.value());
  }

  auto response_header = Header();
  auto cors_allow_origin = std::string_view();
  auto cors_allow_credentials = false;
  for (const auto& [name, value] : header)
    if (iequals(name, "Location")) {
      const auto location = to_absolute_url(value, url);
      response_header.emplace(name,
        to_relative_url(location, m_server_base));
    }
    else if (iequals(name, "Content-Type")) {
      response_header.emplace(name, content_type);
    }
    else if (iequals(name, "Content-Length")) {
      response_header.emplace(name, std::to_string(data.size()));
    }
    else if (iequals(name, "Strict-Transport-Security")) {
      set_strict_transport_security(url,
        (value.find("includeSubDomains") != std::string::npos));
    }
    else if (iequals(name, "Access-Control-Allow-Credentials")) {
      cors_allow_credentials = (value == "true");
    }
    else if (iequals(name, "Access-Control-Allow-Origin")) {
      cors_allow_origin = (value == "*" ? value : m_local_server_base);
    }
    else if (!iequals_any(name,
          "Set-Cookie",
          "Connection",
          "Cache-Control"
          "Link",
          "Transfer-Encoding",
          "Timing-Allow-Origin",
          "Content-Security-Policy",
          "Content-Security-Policy-Report-Only")) {
      response_header.emplace(name, value);
    }

  if (auto it = request.header().find("Origin"); it != request.header().end())
    cors_allow_origin = it->second;

  if (!cors_allow_origin.empty() || cors_allow_credentials) {
    if (cors_allow_credentials) {
      response_header.emplace("Access-Control-Allow-Origin", cors_allow_origin);
      response_header.emplace("Access-Control-Allow-Credentials", "true");
    }
    else {
      response_header.emplace("Access-Control-Allow-Origin", "*");
    }
  }

  response_header.emplace("Connection", "keep-alive");
  response_header.emplace("Cache-Control", "no-store");

  request.send_response(status_code, response_header, data);

  log(Event::served, url);
  if (m_settings.verbose)
    log(Event::info, "served '", url, "' within ", request.age().count(), "ms");
}

void Logic::handle_initial_redirects(const std::string& url,
    const StatusCode status_code, const Header& header) {
  // only evaluate while single threaded
  if (m_start_threads_callback) {
    // update server base on initial redirects
    if (is_redirect(status_code)) {
      if (is_same_url(m_server_base_path, url))
        if (auto it = header.find("Location"); it != header.end()) {
          set_server_base(it->second);
          log(Event::redirect, it->second);
        }
    }
    else {
      auto start_threads = std::exchange(m_start_threads_callback, { });
      start_threads();
    }
  }
}

void Logic::set_strict_transport_security(const std::string& url, bool sub_domains) {
  auto lock = std::lock_guard(m_write_mutex);
  const auto hostname = get_hostname_port(url);
  if (!m_strict_transport_security.count(hostname))
    m_strict_transport_security.emplace(hostname,
      url_to_regex("http://" + std::string(hostname), sub_domains));
}

void Logic::async_write_file(const std::string& identifying_url,
    StatusCode status_code, const Header& header, ByteView data,
    time_t response_time, bool allow_lossy_compression,
    std::function<void(bool)>&& on_complete) {
  auto lock = std::lock_guard(m_write_mutex);
  const auto filename = to_local_filename(identifying_url);
  if (m_archive_writer && !m_archive_writer->contains(filename)) {
    m_header_writer.write(identifying_url, status_code, header);
    if (!data.empty())
      return m_archive_writer->async_write(
        filename, data, response_time, allow_lossy_compression,
        std::move(on_complete));
  }
  on_complete(true);
}

void Logic::append_unrequested_files() {
  if (!m_archive_reader || !m_archive_writer)
    return;

  if (m_settings.archive_policy == ArchivePolicy::requested)
    return;

  // set first overlay, so ArchiveReader::Version top and base are always available
  // top is the first and base is the latest archived
  m_archive_reader->set_overlay_path(first_overlay_path);

  for (const auto& [identifying_url, entry] : m_header_reader.entries()) {
    if (m_blocked_hosts && m_blocked_hosts->contains(identifying_url))
      continue;

    const auto filename = to_local_filename(identifying_url);
    auto base_modification_time = m_archive_writer->get_modification_time(filename);

    if (!base_modification_time.has_value()) {
      // write base
      m_header_writer.write(identifying_url, entry.status_code, entry.header);

      const auto version = (m_settings.archive_policy == ArchivePolicy::first ?
        ArchiveReader::top : ArchiveReader::base);

      if (const auto info = m_archive_reader->get_file_info(filename, version))
        if (auto data = m_archive_reader->read(filename, version); !data.empty()) {
          const auto data_view = ByteView(data);
          m_archive_writer->async_write(filename, data_view, 
            info->modification_time, false, [data = std::move(data)](bool) { });
          base_modification_time = info->modification_time;
        }
    }

    if (m_settings.archive_policy == ArchivePolicy::latest_and_first) {
      // write first
      const auto info = m_archive_reader->get_file_info(filename, ArchiveReader::top);
      if (info && info->modification_time != base_modification_time)
        if (auto data = m_archive_reader->read(filename, ArchiveReader::top); !data.empty()) {
          const auto data_view = ByteView(data);
          m_archive_writer->async_write(first_overlay_path + filename, data_view, 
            info->modification_time, false, [data = std::move(data)](bool) { });
        }
    }
  }
}

FileRequestAction get_file_request_action(const Settings& settings,
    bool archived, bool expired) {

  auto serve = false;
  auto write = false;
  auto download = true;

  if (archived) {
    switch (settings.serve_policy) {
      case ServePolicy::latest:
        if (!expired) {
          serve = true;
          write = true;
          download = false;
        }
        break;

      case ServePolicy::last_archived:
        serve = true;
        if (!expired) {
          write = true;
          download = false;
        }
        break;

      case ServePolicy::first_archived:
        serve = true;
        write = false;
        download = false;
        break;
    }
  }

  if (settings.download_policy == DownloadPolicy::always) {
    if (settings.serve_policy == ServePolicy::latest)
      serve = false;

    write = false;

    if (settings.serve_policy != ServePolicy::first_archived)
      download = true;
  }

  if (settings.download_policy == DownloadPolicy::never) {
    if (archived)
      serve = true;

    if (archived && settings.serve_policy != ServePolicy::first_archived)
      write = true;

    download = false;
  }

  return { serve, write, download };
}
