
#include "HtmlPatcher.h"
#include "HostList.h"
#include "gumbo.h"
#include <stack>
#include <optional>
#include <cctype>
#include <cstring>

HtmlPatcher::HtmlPatcher(
      std::string server_base,
      std::string base_url,
      std::string data,
      std::string inject_js_path,
      std::string cookies,
      time_t response_time)
    : m_server_base(std::move(server_base)),
      m_data(std::move(data)),
      m_cookies(std::move(cookies)),
      m_inject_js_path(std::move(inject_js_path)),
      m_response_time(response_time) {

  update_base_url(base_url);
  parse_html();
}

void HtmlPatcher::parse_html() {
  const auto output = gumbo_parse_with_options(
    &kGumboDefaultOptions, m_data.data(), m_data.size());

  if (output->root->type != GUMBO_NODE_ELEMENT)
    return;
  auto element_stack = std::stack<const GumboNode*>();
  element_stack.push(output->root);

  auto start_of_head = std::optional<const char*>();
  auto has_base_tag = false;

  const auto range = [&](GumboStringPiece piece) {
    return std::string_view(piece.data, piece.length);
  };

  while (!element_stack.empty()) {
    const auto node = element_stack.top();
    element_stack.pop();
    const auto& element = node->v.element;

    switch (element.tag) {
      case GUMBO_TAG_HEAD: {
        if (element.original_tag.data)
          start_of_head = element.original_tag.data + element.original_tag.length;
        break;
      }
      case GUMBO_TAG_BASE:
        if (const auto attrib = gumbo_get_attribute(&element.attributes, "href"))
          apply_base(range(attrib->original_value));
        has_base_tag = true;
        break;

      default:
        break;
    }

    for (const auto name : { "integrity", "crossorigin" })
      if (const auto attrib = gumbo_get_attribute(&element.attributes, name))
        if (*attrib->value) {
          const auto begin = attrib->original_name.data;
          const auto end = attrib->original_value.data + attrib->original_value.length;
          remove_region(range({ begin, static_cast<size_t>(end - begin) }));
        }

    for (auto i = 0u; i < element.children.length; ++i) {
      const auto child = static_cast<const GumboNode*>(element.children.data[i]);
      if (child->type == GUMBO_NODE_ELEMENT)
        element_stack.push(child);
    }
  }
  gumbo_destroy_output(&kGumboDefaultOptions, output);

  if (!start_of_head) {
    // sometimes the HTML is too foobared for gumbo
    // e.g.: https://www.retrogames.cz/play_102-DOS.php
    auto head = std::strstr(m_data.data(), "<head>");
    if (!head)
      head = std::strstr(m_data.data(), "<HEAD>");
    if (head)
      start_of_head = head + 6;
  }

  if (start_of_head) {
    if (!has_base_tag)
      inject_base({ *start_of_head, 0 });
    inject_patch_script({ *start_of_head, 0 });
  }
}

std::string_view HtmlPatcher::get_link(std::string_view at) const {
  auto link = trim(at);

  // remove quotes
  if (link.size() >= 2 && link.front() == link.back() &&
      (link.front() == '"' || link.front() == '\''))
    link = link.substr(1, link.size() - 2);

  if (link.size() >= 12 && starts_with(link, "&quot;") && ends_with(link, "&quot;"))
    link = link.substr(6, link.size() - 12);

  return link;
}

void HtmlPatcher::inject_base(std::string_view at) {
  auto base_script = "<base href='" +
    std::string(get_scheme_hostname_port_path(m_base_url)) + "'>";
  patch(at, std::move(base_script));
}

void HtmlPatcher::apply_base(std::string_view at) {
  const auto link = get_link(at);
  update_base_url(to_absolute_url(link, m_base_url));
  patch(link, m_base_url);
}

void HtmlPatcher::update_base_url(std::string base_url) {
  m_base_url = std::move(base_url);
}

void HtmlPatcher::inject_patch_script(std::string_view at) {
  if (m_inject_js_path.empty())
    return;

  const auto escape_quote = [](auto string) {
    replace_all(string, "'", "\\'");
    return string;
  };
  auto patch_script =
    "<script type='text/javascript'>"
      "__webrecorder = { "
        "server_base:'" + m_server_base + "', "
        "origin:'" + std::string(get_scheme_hostname_port(m_base_url)) + "', "
        "host:'" + std::string(get_hostname_port(m_base_url)) + "', "
        "hostname:'" + std::string(get_hostname(m_base_url)) + "', "
        "cookies:'" + escape_quote(m_cookies) + "', "
        "response_time:" + std::to_string(m_response_time) + ", "
      "}"
    "</script>"
    "<script type='text/javascript' src='" + m_inject_js_path + "'></script>";
  patch(at, std::move(patch_script));
}

void HtmlPatcher::remove_region(std::string_view at) {
  patch(at, "");
}

void HtmlPatcher::patch(std::string_view at, std::string patch) {
  m_patches.push_back({ at, std::move(patch) });
}

std::string HtmlPatcher::get_patched() const {
  auto data = std::string();
  auto patches = m_patches;
  auto pos = m_data.data();
  std::sort(patches.begin(), patches.end(),
    [](const auto &a, const auto &b) { return a.replace.data() < b.replace.data(); });
  for (const auto& patch : patches) {
    // there should not be overlapping ranges, but there are sometimes.
    // e.g. http://fabiensanglard.net/doom3/index.php (<a href="dmap.php">>>)
    if (pos > patch.replace.data())
      continue;

    data.append(pos, patch.replace.data());
    data.append(patch.patch);
    pos = patch.replace.data() + patch.replace.size();
  }
  data.append(pos, m_data.data() + m_data.size());
  return data;
}
