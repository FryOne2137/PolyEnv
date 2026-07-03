# Read the Docs

This repository is ready to be published on Read the Docs.

Read the Docs is free for public open source repositories. The build is configured by `.readthedocs.yaml` in the repository root:

```yaml
version: 2

build:
  os: ubuntu-24.04
  tools:
    python: "3.12"

mkdocs:
  configuration: mkdocs.yml

python:
  install:
    - requirements: docs/requirements.txt
```

## Publish The Documentation

1. Push this repository to GitHub.
2. Open [readthedocs.org](https://readthedocs.org/).
3. Sign in with GitHub.
4. Click **Import a Project**.
5. Select the `PolyEnv` repository.
6. Keep the default settings and confirm.
7. Read the Docs will detect `.readthedocs.yaml` and build the MkDocs site.

After the first successful build, the documentation will be available at a URL like:

```text
https://<project-slug>.readthedocs.io/
```

The exact URL depends on the project slug chosen during import.

## Local Preview

Before pushing changes, preview the docs locally:

```bash
pip install -r docs/requirements.txt
mkdocs serve
```

Build locally in strict mode:

```bash
mkdocs build --strict
```

Strict mode fails the build on broken links or invalid documentation configuration.

