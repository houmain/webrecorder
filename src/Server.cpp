
#include "Server.h"
#include "libs/SimpleWeb/server_http.hpp"

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

std::shared_ptr<asio::io_service> sole_io_service() {
  static auto s_io_service = std::make_shared<asio::io_service>();
  return s_io_service;
}

struct Server::Request::Impl {
  std::shared_ptr<HttpServer::Request> request;
  std::shared_ptr<HttpServer::Response> response;
  ByteVector request_data;
};

struct Server::Impl : public HttpServer {
  HandleRequest handle_request;
  HandleError handle_error;
  std::unique_ptr<asio::signal_set> stop_signals;
  int port{ };

  std::vector<std::thread> threads;
  std::mutex thread_mutex;
  std::exception_ptr thread_exception;

  void start() {
    io_service = sole_io_service();
    stop_signals = std::make_unique<asio::signal_set>(*io_service,
#if defined(__linux__)
      SIGHUP,
#endif
      SIGINT, SIGTERM);

    stop_signals->async_wait(std::bind(&asio::io_service::stop, io_service));

    config.port = 0;
    port = bind();

    default_resource["GET"] =
    default_resource["POST"] =
    default_resource["HEAD"] =
    default_resource["PUT"] =
    default_resource["DELETE"] =
    default_resource["CONNECT"] =
    default_resource["OPTIONS"] =
    default_resource["TRACE"] =
    default_resource["PATCH"] =
      [this](std::shared_ptr<HttpServer::Response> response,
             std::shared_ptr<HttpServer::Request> request) {
        auto request_impl = std::make_unique<::Server::Request::Impl>();
        request_impl->response = std::move(response);
        request_impl->request = std::move(request);
        handle_request({ std::move(request_impl) });
      };

    on_error =
      [this](std::shared_ptr<HttpServer::Request> request,
             const SimpleWeb::error_code& error) {

        if (error == asio::error::operation_aborted)
          return;

        auto request_impl = std::make_unique<::Server::Request::Impl>();
        request_impl->request = std::move(request);
        handle_error({ std::move(request_impl) }, error);
      };

    acceptor->listen();
    accept();
  }

  void run() {
    io_service->run();
  }

  void join_threads() {
    for (auto& thread : threads)
      if (thread.joinable())
        thread.join();
    threads.clear();

    if (thread_exception)
      std::rethrow_exception(thread_exception);
  }

  void run_threads(int thread_count) {
    auto lock = std::lock_guard(thread_mutex);
    for (auto i = 0; i < thread_count; i++)
      threads.emplace_back([this]() noexcept {
        try {
          io_service->run();
        }
        catch (const std::exception& ex) {
          auto lock = std::lock_guard(thread_mutex);
          thread_exception = std::make_exception_ptr(ex);
        }
      });
  }
};

//-------------------------------------------------------------------------

Server::Request::Request(std::unique_ptr<Impl> impl)
  : m_impl(std::move(impl)) {

  auto& data = m_impl->request_data;
  data.resize(m_impl->request->content.size());
  m_impl->request->content.read(
    reinterpret_cast<char*>(data.data()),
    static_cast<std::streamsize>(data.size()));
}

Server::Request::Request(Request&&) = default;
Server::Request& Server::Request::operator=(Request&&) = default;
Server::Request::~Request() = default;

const std::string& Server::Request::method() const {
  return m_impl->request->method;
}

const std::string& Server::Request::path() const {
  return m_impl->request->path;
}

const std::string& Server::Request::query() const {
  return m_impl->request->query_string;
}

const Header& Server::Request::header() const {
  return m_impl->request->header;
}

ByteView Server::Request::data() const {
  return m_impl->request_data;
}

void Server::Request::send_response(StatusCode status_code,
    const Header& header, ByteView data) {
  m_impl->response->write(status_code, as_string_view(data), header);
  m_impl->response.reset();
}

bool Server::Request::response_sent() const {
  return (m_impl->response == nullptr);
}

//-------------------------------------------------------------------------

Server::Server(HandleRequest handle_request,
               HandleError handle_error)
    : m_impl(std::make_unique<Impl>()) {

  m_impl->handle_request = std::move(handle_request);
  m_impl->handle_error = std::move(handle_error);
  m_impl->start();
}

Server::Server(Server&&) = default;
Server& Server::operator=(Server&&) = default;
Server::~Server() = default;

int Server::port() const {
  return m_impl->port;
}

void Server::run_threads(int thread_count) {
  m_impl->run_threads(thread_count);
}

void Server::run() {
  m_impl->run();
  m_impl->join_threads();
}
