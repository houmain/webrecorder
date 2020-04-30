
#include "common.h"
#include <csignal>

template<typename A, typename B>
void eq(const A& a, const B& b) {
  if (!(a == b))
    std::raise(SIGINT);
}

void tests() {
  eq(format_time(0), "Thu, 01 Jan 1970 00:00:00 GMT");
  eq(parse_time("Thu, 01 Jan 1970 00:00:00 GMT"), 0);
  eq(parse_time(format_time(1588262656)), 1588262656);

  eq(trim(" abc "), "abc");
  eq(trim("\t abc \n"), "abc");

  eq(to_local_filename("http://www.a.com"), "http/www.a.com");
  eq(to_local_filename("http://www.a.com/"), "http/www.a.com/index");
  eq(to_local_filename("http://www.a.com/file.txt"), "http/www.a.com/file.txt");
  eq(to_local_filename("http://www.a.com/sub"), "http/www.a.com/sub");
  eq(to_local_filename("http://www.a.com/sub/"), "http/www.a.com/sub/index");
  eq(to_local_filename("http://www.a.com//file.txt"), "http/www.a.com/file.txt");
  eq(to_local_filename("http://www.a.com/sub//"), "http/www.a.com/sub/index");

  eq(filename_from_url("http://www.a.com"), "www.a.com");
  eq(filename_from_url("http://www.a.com/"), "www.a.com");
  eq(filename_from_url("http://www.a.com/file.txt"), "www.a.com╱file.txt");
  eq(filename_from_url("http://www.a.com/sub"), "www.a.com╱sub");
  eq(filename_from_url("http://www.a.com/sub/"), "www.a.com╱sub");
  eq(filename_from_url("http://www.a.com//file.txt"), "www.a.com╱file.txt");
  eq(filename_from_url("http://www.a.com/sub//"), "www.a.com╱sub");

  eq(url_from_input("http://www.a.com"), "http://www.a.com");
  eq(url_from_input("https://www.a.com"), "https://www.a.com");
  eq(url_from_input("www.a.com"), "http://www.a.com");
  eq(url_from_input("www.a.com/file.txt"), "http://www.a.com/file.txt");

  eq(to_relative_url("http://www.a.com/", "http://www.a.com"), "/");
  eq(to_relative_url("http://www.a.com/file.txt", "http://www.a.com"), "/file.txt");
  eq(to_relative_url("http://www.a.com/sub/file.txt", "http://www.a.com"), "/sub/file.txt");
  eq(to_relative_url("http://www.a.com", "http://www.a.com"), "/");

  eq(is_relative_url("http://www.a.com"), false);
  eq(is_relative_url("http://www.a.com/"), false);
  eq(is_relative_url("http://www.a.com/file.txt"), false);
  eq(is_relative_url("file.txt"), true);
  eq(is_relative_url("/http://www.a.com"), true);
  eq(is_relative_url("/file.txt"), true);
  eq(is_relative_url("./file.txt"), true);
  eq(is_relative_url("../file.txt"), true);

  eq(is_same_url("http://www.a.com", "http://www.a.com"), true);
  eq(is_same_url("http://www.a.com", "http://www.b.com"), false);
  eq(is_same_url("http://www.a.com/", "http://www.a.com"), true);
  eq(is_same_url("http://www.a.com/", "http://www.a.com/"), true);

  eq(get_scheme("http://www.a.com"), "http");
  eq(get_scheme("https://www.a.com"), "https");
  eq(get_scheme("data:123"), "data");
  eq(get_scheme("javascript:123"), "javascript");

  eq(to_absolute_url("http://www.a.com/file?query", "http://www.b.com"), "http://www.a.com/file?query");
  eq(to_absolute_url("http://www.b.com/file?query", "http://www.b.com"), "http://www.b.com/file?query");
  eq(to_absolute_url("/http://www.a.com/file?query", "http://www.b.com"), "http://www.b.com/http://www.a.com/file?query");
  eq(to_absolute_url("/http://www.a.com/file?query", "http://www.b.com/"), "http://www.b.com/http://www.a.com/file?query");
  eq(to_absolute_url("/", "http://www.b.com"), "http://www.b.com/");
  eq(to_absolute_url("/", "http://www.b.com/sub"), "http://www.b.com/");
  eq(to_absolute_url("/", "http://www.b.com/sub/"), "http://www.b.com/");
  eq(to_absolute_url("/", "http://www.b.com/sub/index"), "http://www.b.com/");
  eq(to_absolute_url("/file.txt", "http://www.b.com"), "http://www.b.com/file.txt");
  eq(to_absolute_url("/file.txt", "http://www.b.com/sub"), "http://www.b.com/file.txt");
  eq(to_absolute_url("/file.txt", "http://www.b.com/sub/"), "http://www.b.com/file.txt");
  eq(to_absolute_url("/file.txt", "http://www.b.com/sub/index"), "http://www.b.com/file.txt");
  eq(to_absolute_url("/sub/", "http://www.b.com"), "http://www.b.com/sub/");
  eq(to_absolute_url("/sub/", "http://www.b.com/sub"), "http://www.b.com/sub/");
  eq(to_absolute_url("/sub/", "http://www.b.com/sub/"), "http://www.b.com/sub/");
  eq(to_absolute_url("/sub/", "http://www.b.com/sub/index"), "http://www.b.com/sub/");
  eq(to_absolute_url("/sub/file.txt", "http://www.b.com"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("/sub/file.txt", "http://www.b.com/sub"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("/sub/file.txt", "http://www.b.com/sub/"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("/sub/file.txt", "http://www.b.com/sub/index"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("file.txt", "http://www.b.com"), "http://www.b.com/file.txt");
  eq(to_absolute_url("file.txt", "http://www.b.com/sub"), "http://www.b.com/file.txt");
  eq(to_absolute_url("file.txt", "http://www.b.com/sub/"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("file.txt", "http://www.b.com/sub/index"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("./file.txt", "http://www.b.com"), "http://www.b.com/file.txt");
  eq(to_absolute_url("./file.txt", "http://www.b.com/sub"), "http://www.b.com/file.txt");
  eq(to_absolute_url("./file.txt", "http://www.b.com/sub/"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("./file.txt", "http://www.b.com/sub/index"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("sub/", "http://www.b.com"), "http://www.b.com/sub/");
  eq(to_absolute_url("sub/", "http://www.b.com/sub"), "http://www.b.com/sub/");
  eq(to_absolute_url("sub/", "http://www.b.com/sub/"), "http://www.b.com/sub/sub/");
  eq(to_absolute_url("sub/", "http://www.b.com/sub/index"), "http://www.b.com/sub/sub/");
  eq(to_absolute_url("./sub/", "http://www.b.com"), "http://www.b.com/sub/");
  eq(to_absolute_url("./sub/", "http://www.b.com/sub"), "http://www.b.com/sub/");
  eq(to_absolute_url("./sub/", "http://www.b.com/sub/"), "http://www.b.com/sub/sub/");
  eq(to_absolute_url("./sub/", "http://www.b.com/sub/index"), "http://www.b.com/sub/sub/");
  eq(to_absolute_url("sub/file.txt", "http://www.b.com"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("sub/file.txt", "http://www.b.com/sub"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("sub/file.txt", "http://www.b.com/sub/"), "http://www.b.com/sub/sub/file.txt");
  eq(to_absolute_url("sub/file.txt", "http://www.b.com/sub/index"), "http://www.b.com/sub/sub/file.txt");
  eq(to_absolute_url("../file.txt", "http://www.b.com"), "http://www.b.com/file.txt");
  eq(to_absolute_url("../file.txt", "http://www.b.com/sub"), "http://www.b.com/file.txt");
  eq(to_absolute_url("../file.txt", "http://www.b.com/sub/"), "http://www.b.com/file.txt");
  eq(to_absolute_url("../file.txt", "http://www.b.com/sub/index"), "http://www.b.com/file.txt");
  eq(to_absolute_url("../file.txt", "http://www.b.com/sub/sub/"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("../file.txt", "http://www.b.com/sub/sub/index"), "http://www.b.com/sub/file.txt");
  eq(to_absolute_url("data:123", "http://www.b.com/sub/sub/index"), "data:123");
  eq(to_absolute_url("javascript:alert(123)", "http://www.b.com/sub/sub/index"), "javascript:alert(123)");
  eq(to_absolute_url("//www.a.com/file?query", "http://www.b.com"), "http://www.a.com/file?query");
  eq(to_absolute_url("//www.a.com/file?query", "https://www.b.com"), "https://www.a.com/file?query");
  eq(to_absolute_url("//sub/file.txt", "http://www.b.com"), "http://www.b.com//sub/file.txt");
  eq(to_absolute_url("sub//file.txt", "http://www.b.com"), "http://www.b.com/sub//file.txt");

  eq(get_hostname_port("http://www.a.com"), "www.a.com");
  eq(get_hostname_port("http://www.a.com/"), "www.a.com");
  eq(get_hostname_port("http://www.a.com/sub/"), "www.a.com");
  eq(get_hostname_port("http://www.a.com:8080"), "www.a.com:8080");
  eq(get_hostname_port("http://www.a.com:8080/"), "www.a.com:8080");
  eq(get_hostname_port("http://www.a.com:8080/sub"), "www.a.com:8080");

  eq(get_scheme_hostname_port("http://www.a.com"), "http://www.a.com");
  eq(get_scheme_hostname_port("http://www.a.com/"), "http://www.a.com");
  eq(get_scheme_hostname_port("http://www.a.com/sub/"), "http://www.a.com");
  eq(get_scheme_hostname_port("http://www.a.com/sub?query"), "http://www.a.com");
  eq(get_scheme_hostname_port("http://www.a.com:8080"), "http://www.a.com:8080");
  eq(get_scheme_hostname_port("http://www.a.com:8080/"), "http://www.a.com:8080");
  eq(get_scheme_hostname_port("http://www.a.com:8080/sub"), "http://www.a.com:8080");
  eq(get_scheme_hostname_port("http://www.a.com:8080/sub?query"), "http://www.a.com:8080");

  eq(get_scheme_hostname_port_path("http://www.a.com"), "http://www.a.com");
  eq(get_scheme_hostname_port_path("http://www.a.com/"), "http://www.a.com/");
  eq(get_scheme_hostname_port_path("http://www.a.com/sub/"), "http://www.a.com/sub/");
  eq(get_scheme_hostname_port_path("http://www.a.com/sub/file"), "http://www.a.com/sub/file");
  eq(get_scheme_hostname_port_path("http://www.a.com/file?query"), "http://www.a.com/file");
  eq(get_scheme_hostname_port_path("http://www.a.com/file#fragment"), "http://www.a.com/file");
  eq(get_scheme_hostname_port_path("http://www.a.com:8080"), "http://www.a.com:8080");
  eq(get_scheme_hostname_port_path("http://www.a.com:8080/"), "http://www.a.com:8080/");
  eq(get_scheme_hostname_port_path("http://www.a.com:8080/sub/"), "http://www.a.com:8080/sub/");
  eq(get_scheme_hostname_port_path("http://www.a.com:8080/sub/file"), "http://www.a.com:8080/sub/file");
  eq(get_scheme_hostname_port_path("http://www.a.com:8080/file?query"), "http://www.a.com:8080/file");
  eq(get_scheme_hostname_port_path("http://www.a.com:8080/file#fragment"), "http://www.a.com:8080/file");

  eq(get_scheme_hostname_port_path_base("http://www.a.com"), "http://www.a.com");
  eq(get_scheme_hostname_port_path_base("http://www.a.com/"), "http://www.a.com/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com/sub/"), "http://www.a.com/sub/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com/sub/file"), "http://www.a.com/sub/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com/file?query"), "http://www.a.com/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com/file#fragment"), "http://www.a.com/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com:8080"), "http://www.a.com:8080");
  eq(get_scheme_hostname_port_path_base("http://www.a.com:8080/"), "http://www.a.com:8080/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com:8080/sub/"), "http://www.a.com:8080/sub/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com:8080/sub/file"), "http://www.a.com:8080/sub/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com:8080/file?query"), "http://www.a.com:8080/");
  eq(get_scheme_hostname_port_path_base("http://www.a.com:8080/file#fragment"), "http://www.a.com:8080/");

  eq(unpatch_url("/file?query"), "/file?query");
  eq(unpatch_url("/http://www.a.com/file?query"), "http://www.a.com/file?query");
  eq(unpatch_url("http://www.a.com/file?query"), "http://www.a.com/file?query");
}
