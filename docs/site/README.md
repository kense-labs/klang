# The documentation site

The documentation is Markdown that can be read on GitHub, with relative links
between files. This directory uses pandoc to build those files into a static
site for GitHub Pages. It preserves the repository layout so the links keep
working: `README.md` becomes `index.html`, `docs/tutorial.md` becomes
`docs/tutorial.html`, and `samples/README.md` becomes `samples/index.html`. Each
sample program also gets its own page with syntax highlighting.

```bash
python docs/site/build.py --out build/site --check
```

The command needs pandoc 2.11 or later on PATH. The pages are written to
`build/site`; open `build/site/index.html` in a browser. `--check` verifies
every internal link and fragment of the built site, including the `spec §`
links between documents, and fails on a broken one.

| File | Role |
|---|---|
| `build.py` | lists the pages, runs pandoc on each, injects KUI navigation/hero chrome, copies assets and `docs/html`, checks internal links |
| `template.html` | KUI-styled page shell: sticky segmented topbar, collapsible left sidebar, main content frame, command-palette modal, diagram lightbox |
| `style.css` | KUI design system tokens (light/dark themes, accent presets) and component styles |
| `site.js` | theme/accent switching, code block copy headers, interactive hero stage, diagram lightbox, mobile drawer |
| `search.js` | zero-dependency fuzzy search driving the `⌘K` / `/` command palette |
| `links.lua` | a pandoc filter: `.md` links become `.html`, `.k` links point to the sample pages, and the `<details>` table of contents each file carries for GitHub is dropped in favour of the site navigation |
| `klang.xml` | K syntax highlighting for pandoc, in the KDE syntax definition format |

Pandoc reads the files as GitHub-flavored markdown, so heading anchors
are the ones GitHub makes (`#33-the-array-family`), and the cross-document
links in the tutorial and the reference resolve on both.

The workflow `.github/workflows/pages.yml` builds the site on every push
to `main`. It deploys on repositories with Pages enabled. Set the Pages
source to **GitHub Actions** under Settings, Pages, Build and deployment
to enable publishing.

To add a page, write the markdown where it belongs in the repository and
add it to `PAGES` in `build.py` if its directory is not already listed
(every `docs/*.md`, `docs/design/*.md` and `bench/*.md` is picked up).
A page that is already finished HTML goes in `docs/html`, whose files
the build copies as they are, with a link to it from `docs/index.md`.
The site's own index is [`docs/index.md`](../index.md).
