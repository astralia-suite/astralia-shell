# `astralia-shell` knowledges

Hard-won programming techniques and pitfalls from astralia-shell's development, one topic per file.

## Rule

- A topic file opens with a `#` title and one paragraph naming the source paths it covers; `##` headings group its entries.
- One statement + one explanation, ≤ 20 words each.
- A fact lives in exactly one file; no source path is named by two topic intros.
- Design choices belong in `convention.md` and the code, not in a topic file.
- Update an entry when newer knowledge supersedes it, add genuinely new ones, and drop an entry once it is fully superseded.
- Entries describe the merged code base; name a symbol only while it exists.

## Tags

Entries without a tag apply to every backend. Tags `[wayland]` and `[x11]` mark backend-specific rules; where they disagree, the tag says which one applies.
