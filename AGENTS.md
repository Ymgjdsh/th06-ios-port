# Repository delivery rules

- Treat Git history and unique annotated tags as the authoritative version
  record. Do not rely on reused app bundle versions, ZIP names, or nearby
  baseline directories to identify a source version.
- After a requested code change is implemented and verified, do not describe
  the version as delivered until the complete buildable source snapshot has
  been committed and pushed to `origin/main`.
- Publish completed versions with
  `scripts/publish_github_version.ps1 -Message <message> -Tag <unique-tag>`.
  Use a new semantic-version-derived tag for every tester-facing snapshot;
  never move, overwrite, or reuse an existing tag.
- Include source, vendored dependencies, resources, project files, and build
  scripts. Keep generated builds, archives, crash dumps, logs, local runtime
  data, credentials, and signing secrets out of Git.
- Report the commit hash and tag after verifying them against the remote. If
  authentication or the remote push fails, state clearly that only a local
  commit exists and do not claim that GitHub contains the version.
- Analysis-only and diagnostic tasks do not create a release. Do not publish
  unfinished, unverified, or user-rejected changes as completed versions.
