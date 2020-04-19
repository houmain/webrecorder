
#include "Server.h"
#include "Logic.h"
#include "Settings.h"
#include "HostList.h"
#include "platform.h"
#include <filesystem>

extern void tests();

int run(int argc, const char* argv[]) noexcept try {

#if !defined(NDEBUG)
  tests();
#endif

  auto settings = Settings{ };
  if (!interpret_commandline(settings, argc, argv)) {
    print_help_message(argv[0]);
    return 1;
  }

  auto logic = Logic(&settings);

  using namespace std::placeholders;
  auto server = Server(
    std::bind(&Logic::handle_request, &logic, _1),
    std::bind(&Logic::handle_error, &logic, _1, _2));

  const auto path = settings.url.substr(get_scheme_hostname_port(settings.url).size());
  const auto local = "http://127.0.0.1:" + std::to_string(server.port()) + path;
  log(Event::accept, local);

  logic.set_local_server_url(local);
  logic.set_start_threads_callback([&]() { server.run_threads(5); });

  server.run();
  return 0;
}
catch (const std::exception& ex) {
  log(Event::fatal, ex.what());
  return 1;
}
