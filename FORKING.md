# Publishing this fork to GitHub

The local repo is ready: the upstream remote has been renamed to `upstream`,
and all expansion work is committed on the **`nightshade-edition`** branch.

```bash
git remote -v          # upstream -> github.com/omf2097/openomf.git
git branch             # * nightshade-edition
git log --oneline -1   # Nightshade Edition: story expansion, ...
```

## Option A — Fork on GitHub (recommended; keeps the fork relationship)

1. On GitHub, open <https://github.com/omf2097/openomf> and click **Fork**
   (or with the GitHub CLI: `gh repo fork omf2097/openomf --clone=false`).
   Optionally rename your fork to e.g. `openomf-nightshade`.
2. Add your fork as `origin` and push the branch:
   ```bash
   git remote add origin https://github.com/<your-username>/<your-fork>.git
   git push -u origin nightshade-edition
   ```
3. (Optional) Make `nightshade-edition` the default branch in your fork's
   GitHub settings, or open a branch comparison to showcase the changes.

## Option B — Brand-new standalone repo

1. Create an empty repo on GitHub (no README/license, to avoid conflicts).
2. ```bash
   git remote add origin https://github.com/<your-username>/<new-repo>.git
   git push -u origin nightshade-edition
   ```

## Notes

- **Never push to `upstream`** — it points at the original OpenOMF repository.
  The `origin` rename makes accidental pushes there unlikely.
- The freeware game data (`openomf-assets.zip`) and the `build/` tree are
  git-ignored and intentionally not published. Users download the data
  themselves (see [README.md](README.md#game-data)).
- This is a non-profit MIT fan project — keep the credits in
  [README.md](README.md) intact when publishing.
```
