
#include "HostBlocker.h"
#include "common.h"
#include <fstream>
#include <sstream>

bool HostBlocker::add_hosts_from_file(const std::filesystem::path& filename) {
  auto file = std::ifstream(filename);
  if (!file.good())
    return false;
  auto buffer = std::stringstream();
  buffer << file.rdbuf();
  m_hosts_files.emplace_back(buffer.str());
  const auto view = std::string_view(m_hosts_files.back());
  for (auto pos = std::string::size_type{ }; ;) {
    const auto begin = pos;
    pos = view.find('\n', pos);
    if (pos == std::string_view::npos)
      break;
    auto line = view.substr(begin, pos - begin);
    ++pos;

    if (auto hash = line.find('#'); hash != std::string_view::npos)
      line = line.substr(0, hash);
    line = trim(line);
    if (starts_with(line, "0.0.0.0"))
      line = line.substr(7);
    line = trim(line);
    if (!line.empty() && line.find(' ') == std::string_view::npos)
      m_hash_set.emplace(line);
  }
  return true;
}

bool HostBlocker::should_block(std::string_view url) const {
  auto domain = get_hostname_port(url);
  for (;;) {
    if (m_hash_set.find(domain) != m_hash_set.end())
      return true;
    domain = get_without_first_domain(domain);
    if (domain.empty())
      return false;
  }
}
