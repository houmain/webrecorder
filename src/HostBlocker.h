#pragma once

#include <filesystem>
#include <unordered_set>
#include <deque>

class HostBlocker {
public:
  bool add_hosts_from_file(const std::filesystem::path& filename);
  bool has_hosts() const { return !m_hash_set.empty(); }
  bool should_block(std::string_view url) const;

private:
  std::deque<std::string> m_hosts_files;
  std::unordered_set<std::string_view> m_hash_set;
};
