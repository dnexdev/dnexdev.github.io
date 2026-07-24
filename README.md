# Static Site Generator for devlog

Built using jinja and python.

# How to use

1. Clone the repository.
2. Create a virtual environment and install the libraries in `requirements.txt`.
3. To create a post, create a folder where you store all the assets in your post.
4. Write in markdown that `markdown2` can process.
5. Run `python build.py` to generate the site locally.
6. To preview what the website looks like, run `cd docs && python3 -m http.server 8000`.

# Deployment

The site is deployed from GitHub Actions. Pushes to `main` run `build.py` and publish the generated `docs/` folder to GitHub Pages.