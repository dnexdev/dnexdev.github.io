---
title: "Making a blog"
date: 2026-06-11
tags: ["python", "HTML"]
---

Here's how this devlog was created. 

The goal is to make a minimal static site generator for a devlog. 
We want to write a pipeline that takes in a Markdown file and some frontmatter. That Markdown is converted to HTML. We then render into a template.
This output is then written to HTML files. Aftewards we generate an index page holding all the posts.
Finally, we generate an RSS/Atom Feed.

Jinja is very nice. The blog templates are written in Jinja. Our build code simply goes through all the markdown files in the posts folder. Aftewards it converts the Markdown to HTML and Jinja is used to combine everything nicely. The output is a very nice and sleek site.