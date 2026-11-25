# Security

## Reporting a vulnerability

Please report security problems privately, through GitHub's [private vulnerability reporting](https://github.com/heavycircle/dionysus/security/advisories/new) (Security > Report a vulnerability), not in a public issue or pull request.

Say what is affected (the SDK, the `dino` CLI, the Docker image or the release pipeline), the version (`dino version`, or the image tag), and how to reproduce it. A fix is released as a patch release of every supported line, with an advisory crediting you unless you'd rather not be named.

## Supported versions

The newest release gets security fixes. Before 1.0, that's the newest minor release only (0.9.x today); from 1.0 on, the newest minor release of each supported major.

## Verifying a download

Release archives and images are built by GitHub Actions from the tagged commit, with a provenance attestation for each. Check one with the [GitHub CLI](https://cli.github.com/):

```sh
gh attestation verify dino-0.9.0-x86_64-linux.tar.xz -R heavycircle/dionysus
gh attestation verify oci://ghcr.io/heavycircle/dionysus:0.9.0 -R heavycircle/dionysus
```

`SHA256SUMS` next to the archives checks that a download is complete (see [Install](docs/guide/install.md)).
