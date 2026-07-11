# Read the Docs

The repository includes `.readthedocs.yaml`. For a public GitHub repository,
Read the Docs can build this MkDocs site for free.

1. Push the repository to GitHub.
2. Sign in at [readthedocs.org](https://readthedocs.org/) with GitHub.
3. Choose **Import a Project** and select `PolyEnv`.
4. Confirm the default configuration.

Read the Docs installs `docs/requirements.txt` and builds `mkdocs.yml`.

Before pushing documentation changes, verify them locally:

```bash
python -m pip install -r docs/requirements.txt
mkdocs build --strict
```

If the remote build cannot find `docs/requirements.txt`, make sure the file,
`.readthedocs.yaml`, `mkdocs.yml`, and the documentation changes were committed
and pushed.
