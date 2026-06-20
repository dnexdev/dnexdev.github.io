import os
import markdown2
import shutil
from jinja2 import Environment, FileSystemLoader
from datetime import datetime, timezone

env = Environment(loader=FileSystemLoader("./templates/"))

template = env.get_template("post.html")

posts = "./posts"

save_path = "./docs"

SITE_URL = "https://dnex.dev"

post_list = []

# Clean previous build
if os.path.exists(save_path):
    shutil.rmtree(save_path)
os.makedirs(save_path)

for entry in os.scandir(posts):
    if not entry.is_dir():
        continue
    
    post_folder = entry.name
    md_path = os.path.join(entry.path, "post.md")

    with open(md_path, "r") as f:
        content = f.read()
        arr = content.split("---")

        frontmatter = arr[1].splitlines()[1::]

        title = frontmatter[0].split(":")[1].lstrip()[1:-1] 
        date = frontmatter[1].split(":")[1].lstrip()
        tags = eval(frontmatter[2].split(":")[1].lstrip())

        blogtext = arr[2].lstrip()
        output = markdown2.markdown(blogtext)
    
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

    post_content = template.render(log_title=title, page=output)

    with open(os.path.join(out_post_dir, "index.html"), mode="w", encoding="utf-8") as message:
        message.write(post_content)
    
    post_list.append({
        "title": title,
        "date": date,
        "tags": tags,
        "url": f"{post_folder}/"
    })

post_list.sort(key=lambda p: p["date"], reverse=True)

index_template = env.get_template("index.html")
index_content = index_template.render(posts=post_list)

with open(os.path.join(save_path, "index.html"), mode="w", encoding="utf-8") as f:
    f.write(index_content)

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