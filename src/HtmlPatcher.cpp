
#include "HtmlPatcher.h"
#include "HostList.h"
#include "gumbo.h"
#include <stack>
#include <cctype>
#include <string>
#include <fstream>
#include <streambuf>

std::string patch_absolute_url(std::string url) {
  return '/' + url;
}

std::string patch_absolute_url(std::string url, std::string_view base) {
  // try to convert to relative
  if (starts_with(url, base))
    return url.substr(base.size());

  // patch absolute to relative
  return patch_absolute_url(std::move(url));
}

std::string_view unpatch_url(LStringView url) {
  if (is_relative_url(url)) {
    const auto scheme = get_scheme(url.substr(1));
    if (scheme == "http" || scheme == "https")
      return url.substr(1);
  }
  return url;
}

std::string_view get_patch_script_path() {
  return "/__webrecorder.js";
}

std::string_view get_patch_script() {
#include "webrecorder.js.inc"
}

HtmlPatcher::HtmlPatcher(
      std::string server_base,
      std::string base_url,
      std::string mime_type,
      std::string data,
      std::string follow_link_pattern,
      const HostList* bypassed_hosts,
      std::string cookies,
      time_t start_time)
    : m_server_base(std::move(server_base)),
      m_mime_type(mime_type),
      m_data(std::move(data)),
      m_follow_link_pattern(follow_link_pattern),
      m_follow_link_regex(follow_link_pattern),
      m_bypassed_hosts(bypassed_hosts),
      m_cookies(std::move(cookies)),
      m_start_time(start_time) {

  update_base_url(base_url);

  if (starts_with(mime_type, "text/html"))
    parse_html();
  else if (starts_with(mime_type, "text/css"))
    parse_css();
}

void HtmlPatcher::parse_html() {
  const auto output = gumbo_parse_with_options(
    &kGumboDefaultOptions, m_data.data(), m_data.size());

  if (output->root->type != GUMBO_NODE_ELEMENT)
    return;
  auto element_stack = std::stack<const GumboNode*>();
  element_stack.push(output->root);

  auto inject_script_at = m_data.data() + m_data.size();

  const auto range = [&](GumboStringPiece piece) {
    return std::string_view(piece.data, piece.length);
  };

  while (!element_stack.empty()) {
    const auto node = element_stack.top();
    element_stack.pop();
    const auto& element = node->v.element;

    auto is_anchor = false;
    auto link_attribute = std::add_pointer_t<const char>{ };
    switch (element.tag) {
      case GUMBO_TAG_BASE:
        inject_script_at = std::min(inject_script_at, element.original_tag.data);
        if (const auto attrib = gumbo_get_attribute(&element.attributes, "href"))
          apply_base(range(attrib->original_value));
        break;
      case GUMBO_TAG_TITLE:
        for (auto i = 0u; i < element.children.length; ++i) {
          auto child = static_cast<const GumboNode*>(element.children.data[i]);
          if (child->type == GUMBO_NODE_TEXT)
            m_title = std::string(child->v.text.text);
        }
        break;
      case GUMBO_TAG_FORM:
        link_attribute = "action";
        break;
      case GUMBO_TAG_A:
        is_anchor = true;
        link_attribute = "href";
        break;
      case GUMBO_TAG_LINK:
        link_attribute = "href";
        break;
      case GUMBO_TAG_FRAME:
      case GUMBO_TAG_IFRAME:
      case GUMBO_TAG_EMBED:
      case GUMBO_TAG_INPUT:
        link_attribute = "src";
        break;
      case GUMBO_TAG_SCRIPT:
        link_attribute = "src";
        inject_script_at = std::min(inject_script_at, element.original_tag.data);
        break;
      case GUMBO_TAG_IMG:
      case GUMBO_TAG_SOURCE:
        link_attribute = "src";
        if (const auto attrib = gumbo_get_attribute(&element.attributes, "srcset"))
          add_source_set(range(attrib->original_value));
        break;
      case GUMBO_TAG_STYLE:
        for (auto i = 0u; i < element.children.length; ++i) {
          auto child = static_cast<const GumboNode*>(element.children.data[i]);
          if (child->type == GUMBO_NODE_TEXT)
            add_css(range(child->v.text.original_text));
        }
        break;
      default:
        link_attribute = "background";
        break;
    }
    if (link_attribute)
      if (const auto attrib = gumbo_get_attribute(&element.attributes, link_attribute))
        patch_link(range(attrib->original_value), is_anchor);

    for (const auto name : { "integrity", "crossorigin" })
      if (const auto attrib = gumbo_get_attribute(&element.attributes, name))
        if (*attrib->value) {
          const auto begin = attrib->original_name.data;
          const auto end = attrib->original_value.data + attrib->original_value.length;
          remove_region(range({ begin, static_cast<size_t>(end - begin) }));
        }

    if (const auto attrib = gumbo_get_attribute(&element.attributes, "style"))
      add_css(range(attrib->original_value));

    for (auto i = 0u; i < element.children.length; ++i) {
      const auto child = static_cast<const GumboNode*>(element.children.data[i]);
      if (child->type == GUMBO_NODE_ELEMENT)
        element_stack.push(child);
    }
  }
  gumbo_destroy_output(&kGumboDefaultOptions, output);

  if (inject_script_at < m_data.data() + m_data.size())
    inject_patch_script({ inject_script_at, 0 });
}

void HtmlPatcher::add_source_set(std::string_view at) {
  const auto links = get_link(at);
  const auto end = links.end();
  auto it = links.begin();
  while (it < end) {
    auto begin = it;
    it = std::find_if(it, end, [](auto c) { return std::isspace(c); });
    patch_link({ &*begin, static_cast<size_t>(it - begin) });
    // skip size
    while (it != end && std::isspace(*it))
      ++it;
    it = std::find_if(it, end, [](auto c) { return (c == ','); });
    if (it != end)
      ++it;
    while (it != end && std::isspace(*it))
      ++it;
  }
}

void HtmlPatcher::parse_css() {
  add_css(m_data);
}

void HtmlPatcher::add_css(std::string_view at) {
  const auto end = at.end();
  const auto url = std::array<char, 4>{ 'u', 'r', 'l', '(' };
  for (auto it = at.begin();;) {
    it = std::search(it, end, url.begin(), url.end());
    if (it == end)
      break;
    const auto begin = it + 4;
    it = std::find(it, end, ')');
    patch_link({ &*begin, static_cast<size_t>(it - begin) });
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

void HtmlPatcher::apply_base(std::string_view at) {
  const auto link = get_link(at);
  update_base_url(to_absolute_url(link, m_base_url));
  patch(link, patch_absolute_url(m_base_url));
}

void HtmlPatcher::update_base_url(std::string base_url) {
  m_base_url = std::move(base_url);
}

bool HtmlPatcher::should_patch_absolute_url(std::string_view url, bool is_anchor) const {
  if (m_bypassed_hosts && m_bypassed_hosts->contains(url))
    return false;

  if (is_anchor)
    return std::regex_match(url.begin(), url.end(), m_follow_link_regex);

  return true;
}

void HtmlPatcher::patch_link(std::string_view at, bool is_anchor) {
  const auto link = get_link(at);

  // convert to absolute
  auto url = std::string(link);
  if (starts_with(url, "//")) {
    url = std::string(get_scheme(m_base_url)) + ":" + url;
  }
  else if (starts_with(url, "/")) {
    url = to_absolute_url(url, m_base_url);
  }
  else {
    const auto scheme = get_scheme(url);
    if (scheme != "http" && scheme != "https")
      return;
  }

  // conditionally patch to relative url
  if (should_patch_absolute_url(url, is_anchor))
    url = patch_absolute_url(std::move(url), m_server_base);

  patch(link, url);
}

void HtmlPatcher::inject_patch_script(std::string_view at) {
  auto patch_script =
    "<script type='text/javascript'>"
      "__webrecorder_server_base='" + m_server_base + "';"
      "__webrecorder_origin='" + std::string(get_scheme_hostname_port(m_base_url)) + "';"
      "__webrecorder_host='" + std::string(get_hostname_port(m_base_url)) + "';"
      "__webrecorder_hostname='" + std::string(get_hostname(m_base_url)) + "';"
      "__webrecorder_follow_link=/" + m_follow_link_pattern + "/i;"
      "__webrecorder_cookies='" + m_cookies + "';"
      "__webrecorder_start_time=" + std::to_string(m_start_time) + ";"
    "</script>"
    "<script type='text/javascript' src='" +
      std::string(get_patch_script_path()) + "'></script>";
  replace_all(patch_script, "'", "\"");
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
