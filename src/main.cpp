
#include "Server.h"
#include "Logic.h"
#include "Settings.h"
#include "HtmlPatcher.h"
#include "HostBlocker.h"
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

  auto host_blocker = std::make_unique<HostBlocker>();
  for (const auto& file : settings.host_block_lists)
    host_blocker->add_hosts_from_file(file);
  if (!host_blocker->has_hosts())
    host_blocker.reset();

  auto logic = Logic(&settings, std::move(host_blocker));

  using namespace std::placeholders;
  auto server = Server(
    std::bind(&Logic::handle_request, &logic, _1),
    std::bind(&Logic::handle_error, &logic, _1, _2));

  const auto path = settings.url.substr(get_scheme_hostname_port(settings.url).size());
  const auto local = "http://127.0.0.1:" + std::to_string(server.port()) + path;
  log(Event::accept, local);

  logic.setup(local, [&]() { server.run_threads(5); });

  if (settings.open_browser)
    open_browser(local);

  server.run();
  return 0;
}
catch (const std::exception& ex) {
  log(Event::fatal, ex.what());
  return 1;
}
