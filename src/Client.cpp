
#include "Client.h"
#include "libs/SimpleWeb/client_http.hpp"
#include "libs/SimpleWeb/client_https.hpp"
#include "libs/zstr/zstr.hpp"
#include <variant>

using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;
using HttpsClient = SimpleWeb::Client<SimpleWeb::HTTPS>;

extern std::shared_ptr<asio::io_service> sole_io_service();

namespace {
  class GrowBuffer {
  public:
    ~GrowBuffer() {
      std::free(m_data);
    }
    std::byte* data() {
      return m_data;
    }
    std::streamsize capacity() const {
      return m_capacity;
    }
    void grow() {
      m_capacity += (2 << 15);
      m_data = static_cast<std::byte*>(
        std::realloc(m_data, static_cast<size_t>(m_capacity)));
    }

  private:
    std::byte* m_data{ };
    std::streamsize m_capacity{ };
  };
} // namespace

struct Client::Response::Impl {
  std::variant<
    std::shared_ptr<HttpClient::Response>,
    std::shared_ptr<HttpsClient::Response>> response;
  std::error_code error;
  ByteVector data;
};

struct Client::Impl {
  std::shared_ptr<asio::io_service> io_service;
  std::shared_ptr<asio::io_service::work> default_work;
  std::string proxy_server;
  std::thread thread;
};

//-------------------------------------------------------------------------

Client::Response::Response(std::unique_ptr<Impl> impl)
  : m_impl(std::move(impl)) {

  std::visit([&](const auto& response) {
    set_data(response->content, response->content.size(), response->header);
  }, m_impl->response);
}

Client::Response::Response(Response&&) = default;
Client::Response& Client::Response::operator=(Response&&) = default;
Client::Response::~Response() = default;

StatusCode Client::Response::status_code() const {
  auto status_code = 0;
  std::visit([&](const auto& response) {
    status_code = std::atoi(response->status_code.c_str());
  }, m_impl->response);
  return static_cast<StatusCode>(status_code);
}

const Header& Client::Response::header() const {
  auto header = std::add_pointer_t<Header>{ };
  std::visit([&](const auto& response) {
    header = &response->header;
  }, m_impl->response);
  return *header;
}

std::error_code Client::Response::error() const {
  return m_impl->error;
}

ByteView Client::Response::data() const {
  return m_impl->data;
}

void Client::Response::set_data(std::istream& stream, size_t size, Header& header) {
  if (auto it = header.find("content-encoding"); it != header.end()) {
    if (it->second == "gzip") {
      auto zstream = zstr::istream(stream);
      zstream.exceptions(std::ios_base::badbit);

      auto buffer = GrowBuffer();
      auto uncompressed_size = std::streamsize{ };
      while (zstream) {
        if (uncompressed_size == buffer.capacity())
          buffer.grow();
        zstream.read(
          reinterpret_cast<char*>(buffer.data() + uncompressed_size),
          buffer.capacity() - uncompressed_size);
        uncompressed_size += zstream.gcount();
      }
      m_impl->data = { buffer.data(), buffer.data() + uncompressed_size };

      // remove content-encoding and update content-length
      header.erase(it);
      if (it = header.find("content-length"); it != header.end())
        it->second = std::to_string(uncompressed_size);
      return;
    }
    else {
      m_impl->error = std::make_error_code(std::errc::illegal_byte_sequence);
    }
  }
  else {
    m_impl->data.resize(size);
    stream.read(
      reinterpret_cast<char*>(m_impl->data.data()),
      static_cast<std::streamsize>(m_impl->data.size()));
  }
}

//-------------------------------------------------------------------------

Client::Client(std::string proxy_server)
  : m_impl(std::make_unique<Impl>()) {
  m_impl->io_service = sole_io_service();
  m_impl->proxy_server = std::move(proxy_server);
}

Client::Client(Client&&) = default;
Client& Client::operator=(Client&&) = default;
Client::~Client() = default;

void Client::request(std::string_view url, std::string_view method,
    Header header, ByteView data, HandleResponse handle_response) {

  const auto scheme = get_scheme(url);
  const auto hostname_port = std::string(get_hostname_port(url));
  const auto path = url.substr(scheme.size() + 3 + hostname_port.size());

  const auto [begin, end] = header.equal_range("accept-encoding");
  header.erase(begin, end);
  header.emplace("Accept-Encoding", "gzip");

  const auto request = [&](auto client) {
    client->io_service = m_impl->io_service;
    client->config.proxy_server = m_impl->proxy_server;
    client->request(std::string(method), std::string(path), as_string_view(data), header,
      [ client, handle_response = std::move(handle_response)](
          auto response, const std::error_code& error) {
        auto response_impl = std::make_unique<Response::Impl>();
        response_impl->response = std::move(response);
        response_impl->error = error;
        handle_response({ std::move(response_impl) });
      });
  };

  if (scheme == "http")
    request(std::make_shared<HttpClient>(hostname_port));
  else if (scheme == "https")
    request(std::make_shared<HttpsClient>(hostname_port, false));
  else
    throw std::runtime_error("invalid scheme");
}
