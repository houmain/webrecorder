#pragma once

#include "common.h"
#include <functional>

class Client final {
public:
  class Response final {
  public:
    struct Impl;
    Response(std::unique_ptr<Impl> impl);
    Response(Response&&);
    Response& operator=(Response&&);
    ~Response();

    std::error_code error() const;
    StatusCode status_code() const;
    const Header& header() const;
    ByteView data() const;

  private:
    void set_data(std::istream& stream, size_t size, Header& header);

    std::unique_ptr<Impl> m_impl;
  };
  using HandleResponse = std::function<void(Response)>;

  explicit Client(std::string proxy_server = "");
  Client(Client&&);
  Client& operator=(Client&&);
  ~Client();

  void request(std::string_view url, std::string_view method,
    Header header, ByteView data, HandleResponse handle_response);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
