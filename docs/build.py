"""Build the local MkDocs website from the repository's Markdown sources."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

DOCS = Path(__file__).resolve().parent
ROOT = DOCS.parent
PAGES = {"README.md": "index.md", "MANUAL.md": "MANUAL.md",
         "ESP-IDF-BOARDS.md": "ESP-IDF-BOARDS.md"}


def prepare_markdown(text: str, repo_url: str) -> str:
    """Adapt GitHub alerts and repository file links for the generated site."""
    def alert(match: re.Match) -> str:
        kind = match[1].lower()
        body = re.sub(r"^> ?", "", match[2], flags=re.MULTILINE)
        return f'!!! {kind} "{kind.title()}"\n' + "".join(
            f"    {line}\n" for line in body.splitlines()) + "\n"

    text = re.sub(r"^> \[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\n((?:>[^\n]*(?:\n|$))*)",
                  alert, text, flags=re.MULTILINE)

    def link(match: re.Match) -> str:
        target = match[1].replace("\\", "/")
        parts = urlsplit(target)
        if parts.scheme or parts.netloc or not parts.path or target.startswith("/"):
            return match[0]
        if parts.path in PAGES:
            target = PAGES[parts.path] + (f"#{parts.fragment}" if parts.fragment else "")
        elif not parts.path.startswith("images/") and (ROOT / parts.path).is_file():
            target = f"{repo_url.rstrip('/')}/blob/main/{target}"
        return f"]({target})"

    return re.sub(r"\]\(([^\s)]+)\)", link, text)


def main() -> int:
    try:
        import yaml
        import mkdocs  # noqa: F401 -- fail clearly before staging sources
    except ImportError:
        print("MkDocs is missing. Run docs.bat, or install docs/requirements.txt "
              "in your chosen Python environment.", file=sys.stderr)
        return 1

    config = yaml.safe_load((DOCS / "mkdocs.yml").read_text(encoding="utf-8"))
    # Keep generated sources outside the input tree and clean only this newly
    # created temporary directory; the root Markdown and images stay untouched.
    with tempfile.TemporaryDirectory(prefix="p4radio-docs-") as temporary:
        workspace = Path(temporary)
        source = workspace / "source"
        source.mkdir()
        for original, destination in PAGES.items():
            text = (ROOT / original).read_text(encoding="utf-8")
            (source / destination).write_text(
                prepare_markdown(text, config["repo_url"]), encoding="utf-8")
        shutil.copytree(ROOT / "images", source / "images")
        shutil.copy2(DOCS / "style.css", source / "style.css")
        config["docs_dir"] = str(source)
        config["site_dir"] = str(DOCS / "web")
        config_path = workspace / "mkdocs.yml"
        config_path.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
        result = subprocess.run(
            [sys.executable, "-m", "mkdocs", "build", "--strict", "--config-file", str(config_path)],
            cwd=ROOT, check=False)
        if result.returncode:
            return result.returncode
    print(f"Website built: {DOCS / 'web' / 'index.html'}")
    print('For local search, serve it with: python -m http.server 8000 --directory docs/web')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
