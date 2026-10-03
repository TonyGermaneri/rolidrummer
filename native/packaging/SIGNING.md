# macOS signing and notarization

The CMake build signs its AU, VST3 and standalone bundles ad hoc after copying the editor resources. This is suitable for local development. It does not establish a Developer ID or notarization ticket.

A universal release build lives under `native/build-universal/plugin/RoliDrummer_artefacts/Release`. To sign it with a configured Developer ID Application identity, run:

```sh
native/tools/macos-sign.sh native/build-universal/plugin Release
```

Set `MACOS_SIGN_IDENTITY` when more than one identity is installed. The script verifies each bundle after signing. No certificate is imported by this command.

To notarize a release locally, configure `NOTARY_KEY_PATH`, `NOTARY_KEY_ID` and `NOTARY_ISSUER_ID` for an App Store Connect key, or `NOTARY_APPLE_ID`, `NOTARY_PASSWORD` and `NOTARY_TEAM_ID` for the Apple ID route. Then run:

```sh
native/tools/macos-notarize.sh native/build-universal/plugin release Release
```

This command uploads the bundles to Apple, staples accepted tickets, and creates archives. It is an optional release operation; it is not part of the ordinary build or test commands. Credentials are not stored in this repository.

## GitHub Actions

The workflows are adapted from SSB and use the same repository secrets:

| Purpose | Secrets |
| --- | --- |
| Developer ID signing | `MACOS_CERTIFICATE_P12` (base64), `MACOS_CERTIFICATE_PASSWORD`, optional `MACOS_SIGN_IDENTITY` |
| Notarization with App Store Connect | `NOTARY_KEY_P8` (base64), `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID` |
| Alternative Apple ID notarization | `NOTARY_APPLE_ID`, `NOTARY_PASSWORD`, `NOTARY_TEAM_ID` |

The API-key route takes precedence when both notarization methods are configured. Pull-request builds do not receive these credentials. Certificate imports use a temporary keychain, cleaned up even if a later step fails.

Pushes to `main` build and validate universal macOS AU, VST3 and standalone artifacts, signing them when credentials are present. Version tags (`v0.1.0`, for example) additionally require signing/notarization credentials, notarize and staple the bundles, and publish a GitHub Release with archives and SHA-256 checksums. A tag must match both `package.json` and `native/CMakeLists.txt`; update both before tagging a new version. No release is published if validation or notarization fails.

The **Plugins** workflow can also be dispatched manually with **notarize** enabled to produce notarized artifacts without creating a release. Development artifacts report their actual signing/notarization status in `BUILD.txt`. Windows publishing is not configured.

**Deploy to GitHub Pages** tests and builds the editor preview on `main`, then deploys `dist` using the `github-pages` environment. Filesystem access, MIDI and audio still require the native instrument. GitHub Pages must allow the GitHub Actions source, as in SSB; the workflow includes Pages configuration with enablement.
