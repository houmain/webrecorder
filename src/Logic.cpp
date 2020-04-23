
#include "Logic.h"
#include "Settings.h"
#include "HtmlPatcher.h"
#include "HostList.h"
#include "LossyCompressor.h"
#include "platform.h"
#include <random>
#include <sstream>

namespace {
  const auto basic_js_header = Header{ { "Content-Type", "text/javascript;charset=utf-8" } };
  const auto set_cookie_request = "/__webrecorder_setcookie";
  const auto inject_javascript_request = "/__webrecorder.js";

  std::string generate_id(int length = 8) {
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

  std::filesystem::path generate_temporary_filename() {
    auto rand = std::random_device();
    auto filename = std::string("webrecorder_");
    for (auto i = 0; i < 10; i++)
      filename.push_back('0' + rand() % 10);
    filename += ".tmp";
    return std::filesystem::temp_directory_path() / filename;
  }

  std::string get_identifying_url(std::string url, ByteView request_data) {
    if (!request_data.empty()) {
      const auto delim = (url.find('?') != std::string::npos ? '&' : '?');
      url = url + delim + get_hash(request_data);
    }
    return url;
  }

  bool is_redirect(StatusCode status_code) {
    return static_cast<int>(status_code) / 100 == 3;
  }

  std::string url_to_regex(std::string_view url, bool sub_domains = false) {
    auto regex = std::string(url);
    replace_all(regex, "http://", "https?://");
    if (sub_domains)
      replace_all(regex, "://", "://([^/]+.)?");
    replace_all(regex, ".", "\\.");
    replace_all(regex, "/", "\\/");
    return "^" + regex + ".*";
  };

  bool should_serve_from_archive(RefreshPolicy refresh_policy,
      const std::optional<CacheInfo>& cache_info) {
    if (refresh_policy == RefreshPolicy::never)
      return true;
    if (refresh_policy == RefreshPolicy::always)
      return false;
    if (refresh_policy == RefreshPolicy::when_expired ||
        refresh_policy == RefreshPolicy::when_expired_async)
      return (cache_info && !cache_info->expired);
    return false;
  }

  // TODO: could make caching more standard conformant (status code/public...)
  // https://www.w3.org/Protocols/rfc2616/rfc2616-sec13.html
  std::optional<time_t> get_cache_max_age(const Header& reply_header,
      const Header& request_header) {

    const auto find = [](const std::string& string, const char* sequence) -> const char* {
      if (auto pos = std::strstr(string.c_str(), sequence))
        return pos + std::strlen(sequence);
      return nullptr;
    };

    for (const auto& header : { request_header, reply_header }) {
      if (auto it = header.find("Cache-Control"); it != header.end()) {
        if (find(it->second, "no-store"))
          return { };
        if (find(it->second, "no-cache"))
          return 0;
        if (auto value = find(it->second, "s-max-age="))
          return std::atoi(value);
        if (auto value = find(it->second, "max-age="))
          return std::atoi(value);
      }
    }

    if (auto it = reply_header.find("Date"); it != reply_header.end()) {
      const auto date = parse_time(it->second);
      if (auto it = reply_header.find("Expires"); it != reply_header.end())
        return parse_time(it->second) - date;
      if (auto it = reply_header.find("Last-Modified"); it != reply_header.end())
        return (date - parse_time(it->second)) / 10;
    }

    return 0;
  }
} // namespace

Logic::Logic(Settings* settings)
  : m_settings(*settings),
    m_client(m_settings.proxy_server) {

  m_settings.input_file = std::filesystem::u8path(
    get_legal_filename(m_settings.input_file.u8string()));
  m_settings.output_file = std::filesystem::u8path(
    get_legal_filename(m_settings.output_file.u8string()));

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
  }

  if (m_settings.url.empty())
    throw std::runtime_error(m_settings.input_file.empty() ?
      "no URL specified" : "reading file failed");

  if (!m_settings.output_file.empty()) {
    m_archive_writer = std::make_unique<ArchiveWriter>();
    for (auto i = 0; ; ++i) {
      if (m_archive_writer->open(generate_temporary_filename()))
        break;
      if (i > 5)
        throw std::runtime_error("opening temporary file failed");
    }
    m_archive_writer->move_on_close(m_settings.output_file, true);
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

Logic::~Logic() {
  if (m_settings.append && m_archive_reader && m_archive_writer)
    for (const auto& [identifying_url, entry] : m_header_reader.entries()) {
      const auto filename = to_local_filename(identifying_url);
      if (!m_archive_writer->contains(filename))
        if (!m_blocked_hosts || !m_blocked_hosts->contains(identifying_url))
          if (auto data = m_archive_reader->read(filename); !data.empty()) {
            m_header_writer.write(identifying_url, entry.status_code, entry.header);
            const auto file_info = m_archive_reader->get_file_info(filename);
            m_archive_writer->write(filename, data,
              (file_info.has_value() ? file_info->modification_time : 0));
          }
    }
  m_archive_reader.reset();

  if (m_archive_writer) {
    if (m_uid.empty())
      m_uid = generate_id();
    m_archive_writer->write("uid", as_byte_view(m_uid));
    m_archive_writer->write("headers", as_byte_view(m_header_writer.serialize()));
    m_archive_writer->write("cookies", as_byte_view(m_cookie_store.serialize()));

    if (!m_archive_writer->close())
      log(Event::writing_failed);
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

  if (request.method() == "OPTIONS")
    return send_cors_response(std::move(request));

  auto url = to_absolute_url(unpatch_url(request.path()), m_server_base);
  if (!request.query().empty())
    url += "?" + request.query();

  if (get_scheme(url) == "http")
    url = apply_strict_transport_security(std::move(url));

  if (ends_with(request.path(), inject_javascript_request))
    return request.send_response(StatusCode::success_ok,
      basic_js_header, as_byte_view(m_inject_javascript_code));

  if (ends_with(request.path(), set_cookie_request)) {
    m_cookie_store.set(url, as_string_view(request.data()));
    return request.send_response(StatusCode::success_no_content, { }, { });
  }

  if (m_blocked_hosts && m_blocked_hosts->contains(url))
    return serve_blocked(request, url);

  if (serve_from_cache(request, url))
    return;

  auto cache_info = std::optional<CacheInfo>{ };
  if (m_settings.refresh_policy != RefreshPolicy::never)
    cache_info = get_cache_info(request, url);

  if (!m_settings.download || 
      should_serve_from_archive(m_settings.refresh_policy, cache_info)) {
    if (serve_from_archive(request, url, true))
      return;
  }
  else if (m_settings.refresh_policy == RefreshPolicy::when_expired_async) {
    // serve previous version now request a refresh
    serve_from_archive(request, url, false);
  }
  forward_request(std::move(request), url, cache_info);
}

void Logic::send_cors_response(Server::Request request) {
  auto response_header = Header();
  response_header.emplace("Access-Control-Max-Age", "-1");
  for (const auto& [name, value] : request.header())
    if (iequals(name, "Access-Control-Request-Origin")) {
      response_header.emplace("Access-Control-Allow-Origin", m_server_base);
    }
    else if (iequals(name, "Access-Control-Request-Method")) {
      response_header.emplace("Access-Control-Allow-Method", value);
    }
    else if (iequals(name, "Access-Control-Request-Headers")) {
      response_header.emplace("Access-Control-Allow-Headers", value);
    }
  request.send_response(StatusCode::success_no_content, response_header, { });
}

void Logic::forward_request(Server::Request request, const std::string& url,
    const std::optional<CacheInfo>& cache_info) {
  if (!m_settings.download)
    return serve_error(request, url, StatusCode::client_error_not_found);

  log(Event::download_started, url);

  auto header = Header();
  for (const auto& [name, value] : request.header())
    if (iequals_any(name, "Referer", "Origin")) {
      const auto relative_url = unpatch_url(to_relative_url(value, m_local_server_base));
      header.emplace(name, to_absolute_url(relative_url, url));
    }
    else if (!iequals_any(name, "Host", "Accept-Encoding")) {
      header.emplace(name, value);
    }

  m_cookie_store.for_each_cookie(url, [&](const auto& cookie) {
    header.emplace("Cookie", cookie);
  });

  if (cache_info && cache_info->last_modified_time)
    header.emplace("If-Modified-Since", format_time(cache_info->last_modified_time));
  if (cache_info && !cache_info->etag.empty())
    header.emplace("If-None-Match", cache_info->etag);

  const auto& data = request.data();
  const auto& method = request.method();
  m_client.request(url, method, std::move(header), data,
    [ this, url,
      request = std::make_shared<Server::Request>(std::move(request))
    ](Client::Response response) {
      handle_response(*request, url, std::move(response));
    });
}

std::optional<CacheInfo> Logic::get_cache_info(const Server::Request& request,
    const std::string& url) {
  if (!m_archive_reader)
    return { };
  const auto identifying_url = get_identifying_url(url, request.data());
  const auto entry = m_header_reader.read(identifying_url);
  if (!entry)
    return { };

  const auto max_age = get_cache_max_age(entry->header, request.header());
  if (!max_age)
    return { };

  auto age = std::time(nullptr);
  if (auto it = entry->header.find("Date"); it != entry->header.end())
    age -= parse_time(it->second);

  auto cache_info = CacheInfo{ };
  cache_info.expired = (age > max_age.value());

  if (auto it = entry->header.find("Last-Modified"); it != entry->header.end())
    cache_info.last_modified_time = parse_time(it->second);

  if (auto it = entry->header.find("ETag"); it != entry->header.end())
    cache_info.etag = it->second;

  return cache_info;
}

bool Logic::serve_from_cache(Server::Request& request, const std::string& url) {
  auto lock = std::lock_guard(m_write_mutex);
  auto identifying_url = get_identifying_url(url, request.data());
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
  auto identifying_url = get_identifying_url(url, request.data());
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

void Logic::handle_error(Server::Request, std::error_code error) {
  if (const auto message = get_message_utf8(error); !message.empty())
    log(Event::error, message);
}

void Logic::handle_response(Server::Request& request,
    const std::string& url, Client::Response response) {

  const auto status_code = response.status_code();
  if (status_code != StatusCode::success_ok)
    if (serve_from_archive(request, url, true))
      return log(Event::download_omitted, url);

  if (response.error())
    return serve_error(request, url, status_code);

  log(Event::download_finished, static_cast<int>(status_code),
    " ", response.data().size(), " ", url);
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

void Logic::serve_blocked(Server::Request& request, const std::string& url) {
  log(Event::download_blocked, url);
  request.send_response(StatusCode::client_error_not_found, { }, { });
}

void Logic::serve_error(Server::Request& request, const std::string& url,
                        StatusCode status_code) {
  if (status_code == StatusCode::unknown)
    status_code = StatusCode::client_error_not_found;
  log(Event::download_failed, url);
  request.send_response(status_code, { }, { });
}

void Logic::serve_file(Server::Request& request, const std::string& url,
    const StatusCode status_code, const Header& header, ByteView data,
    time_t response_time) {

  if (request.response_sent())
    return;

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

  auto content_length = std::string();
  if (auto it = header.find("Content-Length"); it != header.end())
    content_length = it->second;

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
      m_cookie_store.get_cookies_list(url),
      response_time);

    patched_data.emplace(
      convert_charset(patcher.get_patched(), "utf-8", (charset.empty() ? "utf-8" : charset)));
    data = as_byte_view(patched_data.value());

    if (!content_length.empty())
      content_length = std::to_string(data.size());
  }

  auto response_header = Header();
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
      response_header.emplace(name, content_length);
    }
    else if (iequals(name, "Strict-Transport-Security")) {
      set_strict_transport_security(url,
        (value.find("includeSubDomains") != std::string::npos));
    }
    else if (!iequals_any(name,
          "Set-Cookie",
          "Link",
          "Transfer-Encoding",
          "Access-Control-Allow-Origin",
          "Timing-Allow-Origin",
          "Content-Security-Policy",
          "Content-Security-Policy-Report-Only")) {
      response_header.emplace(name, value);
    }

  // disable browser cache
  const auto [begin, end] = response_header.equal_range("Cache-Control");
  response_header.erase(begin, end);
  response_header.emplace("Cache-Control", "no-store");

  request.send_response(status_code, response_header, data);

  log(Event::served, url);
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

void Logic::set_strict_transport_security(const std::string& url, bool sub_domains) {
  auto lock = std::lock_guard(m_write_mutex);
  const auto hostname = get_hostname_port(url);
  if (!m_strict_transport_security.count(hostname))
    m_strict_transport_security.emplace(hostname,
      url_to_regex("http://" + std::string(hostname), sub_domains));
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
