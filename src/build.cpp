#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct SiteConfig {
  std::string name;
  std::string handle;
  std::string site_url;
  std::string description;
  std::string quote;
  std::string greeting;
  std::string email;
  std::string github;
  std::string x;
  std::string linkedin;
  std::string repo;
  std::vector<std::string> bio;
  std::string now_updated;
  std::vector<std::string> now_paragraphs;
};

struct Project {
  std::string slug;
  std::string title;
  bool featured = true;
  std::string short_desc;
  std::string tagline;
  std::string stat;
  std::vector<std::string> tags;
  std::string github;
  std::string live;
  std::string video;
  std::string poster;
};

std::string trim(std::string_view s) {
  size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  size_t e = s.size();
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return std::string(s.substr(b, e - b));
}

std::string unquote(std::string s) {
  s = trim(s);
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\''))) {
    std::string inner = s.substr(1, s.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
      if (inner[i] == '\\' && i + 1 < inner.size()) {
        char nx = inner[++i];
        if (nx == 'n') out.push_back('\n');
        else if (nx == 't') out.push_back('\t');
        else out.push_back(nx);
      } else {
        out.push_back(inner[i]);
      }
    }
    return out;
  }
  return s;
}

std::vector<std::string> parse_inline_list(std::string s) {
  std::vector<std::string> res;
  s = trim(s);
  if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
    s = s.substr(1, s.size() - 2);
  }
  std::string cur;
  bool in_quotes = false;
  char qchar = 0;
  for (char c : s) {
    if (!in_quotes && (c == '"' || c == '\'')) {
      in_quotes = true;
      qchar = c;
      cur.push_back(c);
    } else if (in_quotes && c == qchar) {
      in_quotes = false;
      cur.push_back(c);
    } else if (!in_quotes && c == ',') {
      std::string item = unquote(cur);
      if (!item.empty()) res.push_back(item);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  std::string item = unquote(cur);
  if (!item.empty()) res.push_back(item);
  return res;
}

std::string escape_html(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot open file: " + p.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_file(const fs::path& p, std::string_view content) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  if (!out) throw std::runtime_error("Cannot write file: " + p.string());
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// Drop a trailing YAML comment: '#' preceded by whitespace, outside quotes.
std::string strip_comment(const std::string& line) {
  char quote = 0;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (quote) {
      if (c == '\\' && quote == '"') ++i;
      else if (c == quote) quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
      return line.substr(0, i);
    }
  }
  return line;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(strip_comment(line));
  }
  return lines;
}

size_t indent_of(std::string_view line) {
  size_t n = 0;
  while (n < line.size() && line[n] == ' ') ++n;
  return n;
}

std::pair<std::string, std::string> split_kv(std::string_view line) {
  auto pos = line.find(':');
  if (pos == std::string_view::npos) return {trim(line), ""};
  return {trim(line.substr(0, pos)), trim(line.substr(pos + 1))};
}

std::string format_date_spaced(std::string iso) {
  for (char& c : iso) {
    if (c == '-') c = ' ';
  }
  return iso;
}

SiteConfig parse_site_yaml(const fs::path& path) {
  SiteConfig cfg;
  auto lines = split_lines(read_file(path));
  enum class Section { Root, Bio, Now, NowParagraphs };
  Section sec = Section::Root;

  for (const auto& raw : lines) {
    std::string t = trim(raw);
    if (t.empty() || t[0] == '#') continue;
    size_t ind = indent_of(raw);

    if (ind == 0) {
      auto [k, v] = split_kv(t);
      if (k == "bio") { sec = Section::Bio; continue; }
      if (k == "now") { sec = Section::Now; continue; }
      sec = Section::Root;
      v = unquote(v);
      if (k == "name") cfg.name = v;
      else if (k == "handle") cfg.handle = v;
      else if (k == "site_url") cfg.site_url = v;
      else if (k == "description") cfg.description = v;
      else if (k == "quote") cfg.quote = v;
      else if (k == "greeting") cfg.greeting = v;
      else if (k == "email") cfg.email = v;
      else if (k == "github") cfg.github = v;
      else if (k == "x") cfg.x = v;
      else if (k == "linkedin") cfg.linkedin = v;
      else if (k == "repo") cfg.repo = v;
    } else if (sec == Section::Bio) {
      if (t.rfind("- ", 0) == 0) cfg.bio.push_back(unquote(t.substr(2)));
    } else if (sec == Section::Now || sec == Section::NowParagraphs) {
      if (t.rfind("- ", 0) == 0) {
        sec = Section::NowParagraphs;
        cfg.now_paragraphs.push_back(unquote(t.substr(2)));
      } else {
        auto [k, v] = split_kv(t);
        if (k == "updated") cfg.now_updated = unquote(v);
        else if (k == "paragraphs") sec = Section::NowParagraphs;
      }
    }
  }
  return cfg;
}

std::vector<Project> parse_projects_yaml(const fs::path& path) {
  std::vector<Project> projects;
  auto lines = split_lines(read_file(path));
  bool in_projects = false;

  for (const auto& raw : lines) {
    std::string t = trim(raw);
    if (t.empty() || t[0] == '#') continue;
    size_t ind = indent_of(raw);

    if (ind == 0) {
      auto [k, _] = split_kv(t);
      in_projects = (k == "projects");
      continue;
    }
    if (!in_projects) continue;

    if (t.rfind("- ", 0) == 0) {
      projects.emplace_back();
      t = trim(t.substr(2));
    }
    if (projects.empty()) continue;

    auto [k, v] = split_kv(t);
    Project& p = projects.back();
    if (k == "slug") p.slug = unquote(v);
    else if (k == "title") p.title = unquote(v);
    else if (k == "featured") p.featured = (unquote(v) != "false");
    else if (k == "short") p.short_desc = unquote(v);
    else if (k == "tagline") p.tagline = unquote(v);
    else if (k == "stat") p.stat = unquote(v);
    else if (k == "tags") p.tags = parse_inline_list(v);
    else if (k == "github") p.github = unquote(v);
    else if (k == "live") p.live = unquote(v);
    else if (k == "video") p.video = unquote(v);
    else if (k == "poster") p.poster = unquote(v);
  }
  return projects;
}

std::string render_page(const SiteConfig& site,
                        const std::string& page_title,
                        const std::string& page_key,
                        const std::string& main_class,
                        const std::string& body_html) {
  auto nav_item = [&](const std::string& href, const std::string& label, const std::string& key) {
    std::string cur = (page_key == key) ? " aria-current=\"page\"" : "";
    return "<a href=\"" + href + "\"" + cur + ">" + label + "</a>";
  };

  constexpr const char* THEME_INIT_HEAD =
      "<style>html{background:#181715;color-scheme:dark}"
      "@media(prefers-color-scheme:light){html:not([data-theme=\"dark\"]){background:#f5f4f1;color-scheme:light}}"
      "html[data-theme=\"light\"]{background:#f5f4f1;color-scheme:light}</style>"
      "<script>(function(){try{var s=localStorage.getItem('dnex-theme');"
      "var t=(s==='light'||s==='dark')?s:(window.matchMedia('(prefers-color-scheme: light)').matches?'light':'dark');"
      "document.documentElement.dataset.theme=t;"
      "var m=document.querySelector('meta[name=\"theme-color\"]');"
      "if(m)m.setAttribute('content',t==='light'?'#f5f4f1':'#181715');}catch(e){}})();</script>";

  std::ostringstream out;
  out << "<!DOCTYPE html>\n"
      << "<html lang=\"en\">\n"
      << "<head>\n"
      << "  <meta charset=\"UTF-8\">\n"
      << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
      << "  <title>" << escape_html(page_title) << "</title>\n"
      << "  <meta name=\"description\" content=\"" << escape_html(site.description) << "\">\n"
      << "  <meta name=\"theme-color\" content=\"#181715\">\n"
      << "  " << THEME_INIT_HEAD << "\n"
      << "  <link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">\n"
      << "  <link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>\n"
      << "  <link href=\"https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=Newsreader:ital,opsz,wght@0,6..72,400;0,6..72,500;1,6..72,400&display=swap\" rel=\"stylesheet\">\n"
      << "  <link rel=\"stylesheet\" href=\"/static/style.css\">\n"
      << "</head>\n"
      << "<body data-page=\"" << escape_html(page_key) << "\">\n"
      << "  <canvas id=\"sim-canvas\" aria-hidden=\"true\"></canvas>\n"
      << "  <div class=\"site-shell\">\n"
      << "    <nav class=\"site-nav\" aria-label=\"Main\">\n"
      << "      " << nav_item("/", "home", "home") << "\n"
      << "      " << nav_item("/projects/", "projects", "projects") << "\n"
      << "      " << nav_item("/now/", "now", "now") << "\n"
      << "      <button type=\"button\" id=\"theme-toggle\" class=\"theme-toggle\" aria-label=\"Switch to light mode\" aria-pressed=\"false\">\n"
      << "        <svg class=\"icon-sun\" xmlns=\"http://www.w3.org/2000/svg\" width=\"15\" height=\"15\" fill=\"currentColor\" viewBox=\"0 0 256 256\" aria-hidden=\"true\"><path d=\"M116,36V20a12,12,0,0,1,24,0V36a12,12,0,0,1-24,0Zm80,92a68,68,0,1,1-68-68A68.07,68.07,0,0,1,196,128Zm-24,0a44,44,0,1,0-44,44A44.05,44.05,0,0,0,172,128ZM51.51,68.49a12,12,0,1,0,17-17l-12-12a12,12,0,0,0-17,17Zm0,119-12,12a12,12,0,0,0,17,17l12-12a12,12,0,1,0-17-17ZM196,72a12,12,0,0,0,8.49-3.51l12-12a12,12,0,0,0-17-17l-12,12A12,12,0,0,0,196,72Zm8.49,115.51a12,12,0,0,0-17,17l12,12a12,12,0,0,0,17-17ZM48,128a12,12,0,0,0-12-12H20a12,12,0,0,0,0,24H36A12,12,0,0,0,48,128Zm80,80a12,12,0,0,0-12,12v16a12,12,0,0,0,24,0V220A12,12,0,0,0,128,208Zm108-92H220a12,12,0,0,0,0,24h16a12,12,0,0,0,0-24Z\"/></svg>\n"
      << "        <svg class=\"icon-moon\" xmlns=\"http://www.w3.org/2000/svg\" width=\"15\" height=\"15\" fill=\"currentColor\" viewBox=\"0 0 256 256\" aria-hidden=\"true\"><path d=\"M236.37,139.4a12,12,0,0,0-12-3A84.07,84.07,0,0,1,119.6,31.59a12,12,0,0,0-15-15A108.86,108.86,0,0,0,49.69,55.07,108,108,0,0,0,136,228a107.09,107.09,0,0,0,64.93-21.69,108.86,108.86,0,0,0,38.44-54.94A12,12,0,0,0,236.37,139.4Zm-49.88,47.74A84,84,0,0,1,68.86,69.51,84.93,84.93,0,0,1,92.27,48.29Q92,52.13,92,56A108.12,108.12,0,0,0,200,164q3.87,0,7.71-.27A84.79,84.79,0,0,1,186.49,187.14Z\"/></svg>\n"
      << "      </button>\n"
      << "    </nav>\n"
      << "    <main class=\"" << escape_html(main_class) << "\">\n"
      << body_html
      << "    </main>\n"
      << "  </div>\n"
      << "  <script src=\"/static/sim.js\" defer></script>\n"
      << "</body>\n"
      << "</html>\n";
  return out.str();
}

std::string render_home_body(const SiteConfig& site) {
  std::ostringstream out;
  out << "      <div class=\"home-content\" data-sim-soften=\"true\">\n";
  if (!site.quote.empty()) {
    out << "        <blockquote class=\"home-quote\">";
    for (char c : site.quote) {
      if (c == '\n') out << "<br>";
      else if (c == '&') out << "&amp;";
      else if (c == '<') out << "&lt;";
      else if (c == '>') out << "&gt;";
      else out << c;
    }
    out << "<span class=\"cursor-blink\" aria-hidden=\"true\"></span></blockquote>\n";
  }
  out << "        <header class=\"home-prose\">\n"
      << "          <h1>" << escape_html(site.greeting) << "</h1>\n";
  for (const auto& para : site.bio) {
    out << "          <p>" << escape_html(para) << "</p>\n";
  }
  out << "          <p class=\"home-links\">\n"
      << "            <a href=\"" << escape_html(site.github) << "\" target=\"_blank\" rel=\"noopener noreferrer\">github</a>"
      << "<span class=\"sep\"> / </span>"
      << "<a href=\"" << escape_html(site.linkedin) << "\" target=\"_blank\" rel=\"noopener noreferrer\">linkedin</a>"
      << "<span class=\"sep\"> / </span>"
      << "<a href=\"" << escape_html(site.x) << "\" target=\"_blank\" rel=\"noopener noreferrer\">x</a>"
      << "<span class=\"sep\"> / </span>"
      << "<a href=\"mailto:" << escape_html(site.email) << "\">email</a>\n"
      << "          </p>\n"
      << "        </header>\n"
      << "      </div>\n";
  return out.str();
}

std::string render_projects_body(const std::vector<Project>& projects) {
  std::ostringstream out;
  out << "      <div class=\"projects-rail\" data-projects-rail tabindex=\"0\" aria-label=\"Projects\">\n";

  for (const auto& p : projects) {
    std::string primary_href = !p.live.empty() ? p.live : p.github;
    out << "        <article class=\"project-card\" id=\"" << escape_html(p.slug) << "\">\n"
        << "          <div class=\"project-media-frame\">\n";
    if (!primary_href.empty()) {
      out << "            <a class=\"project-media-link\" href=\"" << escape_html(primary_href)
          << "\" target=\"_blank\" rel=\"noopener noreferrer\" draggable=\"false\" aria-label=\""
          << escape_html(p.title) << "\">\n";
    }
    if (!p.video.empty()) {
      out << "              <video class=\"project-media\" muted loop playsinline preload=\"metadata\" draggable=\"false\"";
      if (!p.poster.empty()) out << " poster=\"" << escape_html(p.poster) << "\"";
      out << " data-project-video>\n"
          << "                <source src=\"" << escape_html(p.video) << "\" type=\"video/mp4\">\n"
          << "              </video>\n";
    } else if (!p.poster.empty()) {
      out << "              <img class=\"project-media\" src=\"" << escape_html(p.poster)
          << "\" alt=\"" << escape_html(p.title) << "\" loading=\"lazy\" draggable=\"false\">\n";
    }
    if (!primary_href.empty()) {
      out << "            </a>\n";
    }
    out << "            <div class=\"project-overlay-actions\">\n";
    if (!p.live.empty()) {
      out << "              <a class=\"overlay-pill\" href=\"" << escape_html(p.live)
          << "\" target=\"_blank\" rel=\"noopener noreferrer\">view project</a>\n";
    }
    if (!p.github.empty()) {
      out << "              <a class=\"overlay-pill\" href=\"" << escape_html(p.github)
          << "\" target=\"_blank\" rel=\"noopener noreferrer\">source</a>\n";
    }
    out << "            </div>\n"
        << "          </div>\n"
        << "          <div class=\"project-caption\">\n"
        << "            <div class=\"project-caption-head\">\n"
        << "              <h2 class=\"project-title\">";
    if (!primary_href.empty()) {
      out << "<a href=\"" << escape_html(primary_href)
          << "\" target=\"_blank\" rel=\"noopener noreferrer\">" << escape_html(p.title) << "</a>";
    } else {
      out << escape_html(p.title);
    }
    out << "</h2>\n"
        << "              <span class=\"project-stat\">" << escape_html(p.stat) << "</span>\n"
        << "            </div>\n"
        << "            <p class=\"project-tagline\">" << escape_html(p.tagline) << "</p>\n"
        << "          </div>\n"
        << "        </article>\n\n";
  }

  out << "      </div>\n";
  return out.str();
}

std::string render_now_body(const SiteConfig& site) {
  std::ostringstream out;
  out << "      <article class=\"now-article\" data-sim-soften=\"true\">\n"
      << "        <h1>Now</h1>\n"
      << "        <p class=\"now-date\">" << escape_html(format_date_spaced(site.now_updated)) << "</p>\n";
  for (const auto& para : site.now_paragraphs) {
    out << "        <p>" << escape_html(para) << "</p>\n";
  }
  out << "        <p class=\"now-footnote\">inspired by <a href=\"https://nownownow.com/about\" target=\"_blank\" rel=\"noopener noreferrer\">nownownow</a></p>\n"
      << "      </article>\n";
  return out.str();
}

std::string render_redirect(const std::string& to) {
  return "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
         "<style>html{background:#181715;color-scheme:dark}"
         "@media(prefers-color-scheme:light){html:not([data-theme=\"dark\"]){background:#f5f4f1;color-scheme:light}}"
         "html[data-theme=\"light\"]{background:#f5f4f1;color-scheme:light}</style>"
         "<script>(function(){try{var s=localStorage.getItem('dnex-theme');"
         "var t=(s==='light'||s==='dark')?s:(window.matchMedia('(prefers-color-scheme: light)').matches?'light':'dark');"
         "document.documentElement.dataset.theme=t;}catch(e){}})();</script>"
         "<meta http-equiv=\"refresh\" content=\"0; url=" + to + "\">"
         "<link rel=\"canonical\" href=\"" + to + "\"><title>Redirecting…</title>"
         "</head><body></body></html>\n";
}

std::string render_llms_txt(const SiteConfig& site, const std::vector<Project>& projects) {
  std::ostringstream out;
  out << "# " << site.name << "\n\n"
      << "> " << site.description << "\n\n"
      << "- Website: " << site.site_url << "\n"
      << "- GitHub: " << site.github << "\n"
      << "- X: " << site.x << "\n"
      << "- LinkedIn: " << site.linkedin << "\n"
      << "- Email: " << site.email << "\n\n"
      << "## Bio\n\n";
  for (const auto& para : site.bio) out << para << "\n\n";
  out << "## Projects\n\n";
  for (const auto& p : projects) {
    const std::string& primary = !p.live.empty() ? p.live : p.github;
    out << "- " << p.title << ": " << p.tagline << " (" << primary << ")\n";
  }
  out << "\n## Now (" << site.now_updated << ")\n\n";
  for (const auto& para : site.now_paragraphs) out << para << "\n\n";
  return out.str();
}

void copy_static_assets(const fs::path& src, const fs::path& dst) {
  if (!fs::exists(src)) return;
  fs::create_directories(dst);
  for (const auto& entry : fs::recursive_directory_iterator(src)) {
    auto rel = fs::relative(entry.path(), src);
    auto target = dst / rel;
    if (entry.is_directory()) {
      fs::create_directories(target);
    } else if (entry.is_regular_file()) {
      if (entry.path().extension() == ".md") continue;
      fs::create_directories(target.parent_path());
      fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
    }
  }
}

} // namespace

int main() {
  auto t0 = std::chrono::steady_clock::now();

  SiteConfig site = parse_site_yaml("./site.yaml");
  std::vector<Project> projects = parse_projects_yaml("./projects.yaml");

  const fs::path stage_dir = "./.docs-tmp";
  const fs::path out_dir = "./docs";
  if (fs::exists(stage_dir)) fs::remove_all(stage_dir);
  fs::create_directories(stage_dir);

  write_file(stage_dir / "index.html",
             render_page(site, site.name, "home", "page-home", render_home_body(site)));
  write_file(stage_dir / "projects" / "index.html",
             render_page(site, "Projects — " + site.name, "projects", "page-projects",
                         render_projects_body(projects)));
  write_file(stage_dir / "now" / "index.html",
             render_page(site, "Now — " + site.name, "now", "page-now", render_now_body(site)));

  write_file(stage_dir / "blog" / "index.html", render_redirect("/"));
  write_file(stage_dir / "writing" / "index.html", render_redirect("/"));
  write_file(stage_dir / "llms.txt", render_llms_txt(site, projects));

  copy_static_assets("./static", stage_dir / "static");

  const fs::path backup_dir = "./.docs-old";
  if (fs::exists(backup_dir)) fs::remove_all(backup_dir);
  bool had_old = fs::exists(out_dir);
  if (had_old) fs::rename(out_dir, backup_dir);
  fs::rename(stage_dir, out_dir);
  if (had_old) fs::remove_all(backup_dir);

  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::cout << "dnex ssg (c++20): built " << projects.size() << " projects -> ./docs in "
            << std::fixed << std::setprecision(2) << ms << " ms\n";
  return 0;
}
