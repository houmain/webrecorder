#pragma once

#include "libs/SimpleWeb/utility.hpp"
#include "common.h"
#include <functional>

using StatusCode = SimpleWeb::StatusCode;
using Header = SimpleWeb::CaseInsensitiveMultimap;

inline bool is_success(StatusCode status_code) {
  return (static_cast<int>(status_code) / 100 == 2);
}

inline bool is_redirect(StatusCode status_code) {
  return (status_code == StatusCode::redirection_moved_permanently);
}

class Server final {
public:
  class Request final {
  public:
    struct Impl;
    Request(std::unique_ptr<Impl> impl);
    Request(Request&&);
    Request& operator=(Request&&);
    ~Request();

    std::chrono::milliseconds age() const;
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
  using HandleAccepting = std::function<void(unsigned short)>;
  using HandleRequest = std::function<void(Request)>;
  using HandleError = std::function<void(Request, std::error_code)>;

  Server(HandleRequest handle_request, HandleError handle_error);
  Server(Server&&);
  Server& operator=(Server&&);
  ~Server();

  int port() const;
  void run(const HandleAccepting& handle_accepting);
  void run_threads(int thread_count);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
