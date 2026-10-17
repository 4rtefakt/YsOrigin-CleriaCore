# Code signing (SignPath)

The release workflow (`.github/workflows/release.yml`) signs the Windows package's `cleria-view.exe` and
`cleria-gamedata.exe` through [SignPath](https://about.signpath.io/) with SignPath Foundation's free certificate
for open-source projects. Until the repository is set up as below, the package ships unsigned.

Setup, once SignPath Foundation has accepted the project:

1. In SignPath, the project's slug is `CleriaCore`, with the signing policies `test-signing` and
   `release-signing`, and the trusted build system *GitHub.com* linked to the project.
2. The project's default artifact configuration is [artifact-configuration.xml](artifact-configuration.xml).
3. Each signing policy gets the pipeline policy [pipeline-policy.yml](pipeline-policy.yml) (level *Enforce*).
4. On GitHub (Settings > Secrets and variables > Actions): the secret `SIGNPATH_API_TOKEN` (a SignPath API
   token of a user with submitter rights on both policies) and the variable `SIGNPATH_ORGANIZATION_ID`.
5. Install the [SignPath GitHub App](https://github.com/apps/signpath) on the repository.

A release then waits in the workflow until its signing request is approved in SignPath.
