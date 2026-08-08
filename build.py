import os
import markdown2
import shutil
import ast
import yaml
from jinja2 import Environment, FileSystemLoader
from datetime import datetime, timezone

env = Environment(loader=FileSystemLoader("./templates/"))

posts = "./posts"
save_path = "./docs"
SITE_URL = "https://dnex.dev"

post_list = []

# Clean previous build
if os.path.exists(save_path):
    shutil.rmtree(save_path)
os.makedirs(save_path)

with open("./projects.yaml", "r", encoding="utf-8") as f:
    portfolio = yaml.safe_load(f)

featured = [p for p in portfolio["projects"] if p.get("featured")]
secondary = [p for p in portfolio["projects"] if not p.get("featured")]

for entry in os.scandir(posts):
    if not entry.is_dir():
        continue

    post_folder = entry.name
    md_path = os.path.join(entry.path, "post.md")

    with open(md_path, "r") as f:
        content = f.read()
        arr = content.split("---", 2)
        if len(arr) != 3:
            raise ValueError(f"Invalid frontmatter format in {md_path}")

        frontmatter = arr[1].splitlines()[1::]

        title = frontmatter[0].split(":")[1].lstrip()[1:-1]
        date = frontmatter[1].split(":")[1].lstrip()
        tags = ast.literal_eval(frontmatter[2].split(":")[1].lstrip())

        blogtext = arr[2].lstrip()
        output = markdown2.markdown(
            blogtext,
            extras={
                "fenced-code-blocks": {"noclasses": True},
                "latex": {},
            },
        )

    out_post_dir = os.path.join(save_path, post_folder)
    os.makedirs(out_post_dir, exist_ok=True)

    for item in os.scandir(entry.path):
        if item.name == "post.md":
            continue

        src = item.path
        dst = os.path.join(out_post_dir, item.name)
        if item.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True)
        else:
            shutil.copy2(src, dst)

    template = env.get_template("post.html")
    post_content = template.render(log_title=title, page=output)

    with open(os.path.join(out_post_dir, "index.html"), mode="w", encoding="utf-8") as message:
        message.write(post_content)

    post_list.append({
        "title": title,
        "date": date,
        "tags": tags,
        "url": f"{post_folder}/",
    })

post_list.sort(key=lambda p: p["date"], reverse=True)

home_template = env.get_template("home.html")
home_content = home_template.render(
    hero=portfolio["hero"],
    featured=featured,
    secondary=secondary,
    posts=post_list[:3],
)
with open(os.path.join(save_path, "index.html"), mode="w", encoding="utf-8") as f:
    f.write(home_content)

blog_dir = os.path.join(save_path, "blog")
os.makedirs(blog_dir, exist_ok=True)
blog_template = env.get_template("blog.html")
blog_content = blog_template.render(posts=post_list)
with open(os.path.join(blog_dir, "index.html"), mode="w", encoding="utf-8") as f:
    f.write(blog_content)

if os.path.exists("./static"):
    shutil.copytree("./static", os.path.join(save_path, "static"), dirs_exist_ok=True)

atom_template = env.get_template("atom.xml")
atom_content = atom_template.render(
    posts=post_list,
    site_url=SITE_URL,
    updated=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
)

with open(os.path.join(save_path, "atom.xml"), "w", encoding="utf-8") as f:
    f.write(atom_content)
