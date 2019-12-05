#pragma once

#include "common.h"
#include <functional>

class Server final {
public:
  class Request final {
  public:
    struct Impl;
    Request(std::unique_ptr<Impl> impl);
    Request(Request&&);
    Request& operator=(Request&&);
    ~Request();

    const std::string& method() const;
    const std::string& path() const;
    const std::string& query() const;
    const Header& header() const;
    ByteView data() const;

    void send_response(StatusCode status_code,
      const Header& header, ByteView data);
    bool response_sent() const;

  private:
    std::unique_ptr<Impl> m_impl;
  };
  using HandleRequest = std::function<void(Request)>;
  using HandleError = std::function<void(Request, std::error_code)>;

  Server(HandleRequest handle_request, HandleError handle_error);
  Server(Server&&);
  Server& operator=(Server&&);
  ~Server();

  int port() const;
  void run();
  void run_threads(int thread_count);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
