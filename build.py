import os
import markdown2
from jinja2 import Environment, FileSystemLoader

env = Environment(loader=FileSystemLoader("./templates/"))

template = env.get_template("post.html")

posts = "./posts"

save_path = "./docs"

post_list = []

for log in os.scandir(posts):
    if log.is_file():
        with open(log.path, "r") as f:
            content = f.read()
            arr = content.split("---")

            frontmatter = arr[1].splitlines()[1::]

            title = frontmatter[0].split(":")[1].lstrip()[1:-1] 
            # print(title)
            date = frontmatter[1].split(":")[1].lstrip()
            # print(date)
            tags = eval(frontmatter[2].split(":")[1].lstrip())
            # print(tags)

            blogtext = arr[2].lstrip()
            output = markdown2.markdown(blogtext)

            filename = f"{date}-{title}.html"
            completename = os.path.join(save_path, filename)
            post_content = template.render(log_title=title, page=output)

            with open(completename, mode="w", encoding="utf-8") as message:
                message.write(post_content)
            
            post_list.append({
                "title": title,
                "date": date,
                "tags": tags,
                "url": f"{filename}"
            })

post_list.sort(key=lambda p: p["date"], reverse=True)

index_template = env.get_template("index.html")
index_content = index_template.render(posts=post_list)

with open(os.path.join(save_path, "index.html"), mode="w", encoding="utf-8") as f:
    f.write(index_content)