# Building the documentation

Run `docs.bat` from the repository root (or by its full path). It creates a
project-local Python environment in `docs/.venv`, installs the pinned MkDocs
dependency and builds the website into `docs/web`.

Open `docs/web/index.html` to browse the result. For working search, serve it
locally from the repository root:

```sh
python -m http.server 8000 --directory docs/web
```

Then visit <http://localhost:8000>. Stop the server with Ctrl+C.

If MkDocs is already installed in your chosen Python environment, you can build
directly with `python docs/build.py`.

The site uses MkDocs' built-in `readthedocs` theme. Edit `docs/mkdocs.yml` for
navigation and theme settings, and `docs/style.css` for small style adjustments.
The builder stages the root `README.md`, `MANUAL.md`, `ESP-IDF-BOARDS.md` and
images in a temporary directory. Edit those original files, rather than the
generated HTML. GitHub alert blocks are converted to admonitions during the
build, and links to repository configuration/source files point to GitHub.

Each build replaces the generated contents of `docs/web`. The output and the
Python environment are ignored by Git. Nothing is published or served by the
build command itself.
