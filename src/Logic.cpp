
#include "Logic.h"
#include "Settings.h"
#include "HtmlPatcher.h"
#include "HostBlocker.h"
#include "platform.h"
#include <random>

namespace {
  const auto basic_js_header = Header{ { "Content-Type", "text/javascript" } };

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

  std::string get_follow_link_regex(FollowLinkPolicy follow_link_policy,
      const std::string& url) {
    switch (follow_link_policy) {
      case FollowLinkPolicy::none:
        return "^$";
      case FollowLinkPolicy::same_hostname:
        return url_to_regex(get_scheme_hostname_port(url));
      case FollowLinkPolicy::same_second_level_domain:
        return url_to_regex(get_scheme_hostname_port(url), true);
      case FollowLinkPolicy::same_path:
        return url_to_regex(get_scheme_hostname_port_path_base(url));
      case FollowLinkPolicy::all:
        return ".*";
    }
    return "";
  }

  bool should_serve_from_archive(ValidationPolicy validation_policy,
      const std::optional<CacheInfo>& cache_info) {
    if (validation_policy == ValidationPolicy::never)
      return true;
    if (validation_policy == ValidationPolicy::always)
      return false;
    if (validation_policy == ValidationPolicy::when_expired ||
        validation_policy == ValidationPolicy::when_expired_reload)
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

Logic::Logic(Settings* settings, std::unique_ptr<HostBlocker> host_blocker)
  : m_settings(*settings),
    m_host_blocker(std::move(host_blocker)),
    m_client(m_settings.proxy_server) {

  m_settings.filename = std::filesystem::u8path(
    get_legal_filename(m_settings.filename.u8string()));

  auto archive_reader = std::make_unique<ArchiveReader>();
  if (archive_reader->open(m_settings.filename))
    m_settings.filename_from_title = false;

  if (m_settings.url.empty())
    if (auto data = archive_reader->read("url"); !data.empty())
      m_settings.url = std::string(as_string_view(data));

  if (m_settings.url.empty())
    throw std::runtime_error("reading file failed");

  if (m_settings.read) {
    m_archive_reader = std::move(archive_reader);
    if (auto data = m_archive_reader->read("headers"); !data.empty())
      m_header_reader.deserialize(as_string_view(data));
    if (auto data = m_archive_reader->read("cookies"); !data.empty())
      m_cookie_store.deserialize(as_string_view(data));
  }

  if (m_settings.write) {
    m_archive_writer = std::make_unique<ArchiveWriter>();
    for (auto i = 0; ; ++i) {
      if (m_archive_writer->open(generate_temporary_filename()))
        break;
      if (i > 5)
        throw std::runtime_error("opening temporary file failed");
    }
    m_archive_writer->move_on_close(m_settings.filename, true);
    m_archive_writer->write("url", as_byte_view(m_settings.url));
  }

  set_server_base(m_settings.url);
}

Logic::~Logic() {
  if (m_settings.append && m_archive_reader && m_archive_writer)
    for (const auto& [identifying_url, entry] : m_header_reader.entries()) {
      const auto filename = to_local_filename(identifying_url);
      if (!m_archive_writer->contains(filename)) {
        if (auto data = m_archive_reader->read(filename); !data.empty()) {
          m_header_writer.write(identifying_url, entry.status_code, entry.header);
          const auto modification_time = 
            m_archive_reader->get_modification_time(filename);
          m_archive_writer->write(filename, data, modification_time);
        }
      }
    }
  m_archive_reader.reset();

  if (m_archive_writer) {
    m_archive_writer->write("headers", as_byte_view(m_header_writer.serialize()));
    m_archive_writer->write("cookies", as_byte_view(m_cookie_store.serialize()));

    if (!m_archive_writer->close())
      log(Event::writing_failed);
  }
}

void Logic::setup(std::string local_server_url,
           std::function<void()> start_threads_callback) {
  m_local_server_base = get_scheme_hostname_port(local_server_url);
  m_start_threads_callback = std::move(start_threads_callback);
}

void Logic::set_server_base(const std::string& url) {
  m_server_base = get_scheme_hostname_port(url);
  m_server_base_path = get_scheme_hostname_port_path(url);
  m_follow_link_regex = get_follow_link_regex(
    m_settings.follow_link_policy, url);
}

void Logic::handle_request(Server::Request request) {
  if (request.path() == get_patch_script_path())
    return request.send_response(StatusCode::success_ok,
      basic_js_header, as_byte_view(get_patch_script()));

  auto url = to_absolute_url(unpatch_url(request.path()), m_server_base);
  if (!request.query().empty())
    url += "?" + request.query();

  if (get_scheme(url) == "http")
    url = apply_strict_transport_security(std::move(url));

  if (ends_with(request.path(), "__webrecorder_setcookie")) {
    m_cookie_store.set(url, as_string_view(request.data()));
    return request.send_response(StatusCode::success_no_content, { }, { });
  }

  if (m_host_blocker && m_host_blocker->should_block(url))
    return serve_blocked(request, url);

  if (serve_from_cache(request, url))
    return;

  auto cache_info = std::optional<CacheInfo>{ };
  if (m_settings.validation_policy != ValidationPolicy::never)
    cache_info = get_cache_info(request, url);

  if (!m_settings.download || 
      should_serve_from_archive(m_settings.validation_policy, cache_info)) {
    if (serve_from_archive(request, url, true))
      return;
  }
  else if (m_settings.validation_policy == ValidationPolicy::when_expired_reload) {
    // serve previous version now request a refresh
    serve_from_archive(request, url, false);
  }
  forward_request(std::move(request), url, cache_info);
}

void Logic::forward_request(Server::Request request, const std::string& url,
    const std::optional<CacheInfo>& cache_info) {
  if (!m_settings.download)
    return serve_error(request, url, StatusCode::client_error_not_found);

  log(Event::download_started, url);

  auto header = Header();
  for (const auto& [name, value] : request.header())
    if (iequals_any(name, "referer", "origin")) {
      const auto relative_url = unpatch_url(to_relative_url(value, m_local_server_base));
      header.emplace(name, to_absolute_url(relative_url, url));
    }
    else if (!iequals_any(name, "host", "accept-encoding")) {
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
  if (m_archive_writer && m_archive_writer->contains(filename)) {
    auto entry = m_header_writer.read(identifying_url);
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
  const auto response_time = m_archive_reader->get_modification_time(filename);
  auto data = m_archive_reader->read(filename);
  serve_file(request, url, entry->status_code, entry->header, data, response_time);

  if (write_to_archive) {
    auto data_view = ByteView(data);
    async_write_file(identifying_url,
      entry->status_code, entry->header, data_view, response_time,
      [data = std::move(data)](bool succeeded) {
        if (!succeeded)
          log(Event::writing_failed);
      });
  }
  return true;
}

void Logic::handle_error(Server::Request, std::error_code error) {
  log(Event::error, get_message_utf8(error));
}

void Logic::handle_response(Server::Request& request,
    const std::string& url, Client::Response response) {

  const auto status_code = response.status_code();
  if (status_code != StatusCode::success_ok)
    if (serve_from_archive(request, url, true))
      return log(Event::download_omitted, url);

  if (response.error())
    return serve_error(request, url, status_code);

  log(Event::download_finished, static_cast<int>(status_code), " ", url);
  const auto response_time = std::time(nullptr);

  serve_file(request, url, status_code,
    response.header(), response.data(), response_time);

  const auto& header = response.header();
  const auto& data = response.data();
  async_write_file(get_identifying_url(url, request.data()),
    status_code, header, data, response_time,
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
      log(Event::info, "starting io threads");
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

  // cookies are stored by webrecorder and accessible using javascript
  auto [cookie_begin, cookie_end] = header.equal_range("Set-Cookie");
  for (auto it = cookie_begin; it != cookie_end; ++it)
    m_cookie_store.set(url, it->second);

  auto patched_data = std::optional<std::string>();
  if (!data.empty() && iequals_any(mime_type, "text/html", "text/css")) {
    const auto patcher = HtmlPatcher(m_server_base, url, std::string(mime_type),
      convert_charset(data, charset, "utf-8"),
      m_follow_link_regex,
      m_cookie_store.get_cookies_list(url),
      response_time);

    patched_data.emplace(convert_charset(patcher.get_patched(), "utf-8", charset));
    data = as_byte_view(patched_data.value());

    if (!content_length.empty())
      content_length = std::to_string(data.size());

    if (m_settings.filename_from_title &&
        status_code == StatusCode::success_ok &&
        !patcher.title().empty())
      set_filename_from_title(patcher.title());
  }

  auto response_header = Header();
  for (const auto& [name, value] : header)
    if (iequals(name, "Location")) {
      response_header.emplace(name,
        to_relative_or_patch_to_absolute_url(
          to_absolute_url(value, url), m_server_base));
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
          "Transfer-Encoding",
          "Content-Security-Policy",
          "Content-Security-Policy-Report-Only")) {
      response_header.emplace(name, value);
    }

  // disable browser cache
  const auto [begin, end] = response_header.equal_range("Cache-Control");
  response_header.erase(begin, end);
  response_header.emplace("Cache-Control", "no-store");

  request.send_response(status_code, response_header, data);
}

void Logic::async_write_file(const std::string& identifying_url,
    StatusCode status_code, const Header& header, ByteView data,
    time_t response_time, std::function<void(bool)>&& on_complete) {
  auto lock = std::lock_guard(m_write_mutex);
  const auto filename = to_local_filename(identifying_url);
  if (m_archive_writer && !m_archive_writer->contains(filename)) {
    m_header_writer.write(identifying_url, status_code, header);
    if (!data.empty())
      return m_archive_writer->async_write(
        filename, data, response_time, std::move(on_complete));
  }
  on_complete(true);
}

void Logic::set_filename_from_title(const std::string& title_) {
  auto lock = std::lock_guard(m_write_mutex);
  if (!std::exchange(m_filename_from_title_set, true)) {
    const auto title = std::string(trim(title_));
    auto filename = m_settings.filename;
    filename.replace_filename(std::filesystem::u8path(get_legal_filename(title)));
    m_archive_writer->move_on_close(filename, false);
  }
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