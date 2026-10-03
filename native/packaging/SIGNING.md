# macOS signing and notarization

The CMake build signs its AU, VST3 and standalone bundles ad hoc after copying the editor resources. This is suitable for local development. It does not establish a Developer ID or notarization ticket.

A universal release build lives under `native/build-universal/plugin/RoliDrummer_artefacts/Release`. To sign it with a configured Developer ID Application identity, run:

```sh
native/tools/macos-sign.sh native/build-universal/plugin Release
```

Set `MACOS_SIGN_IDENTITY` when more than one identity is installed. The script verifies each bundle after signing. No certificate is imported by this command.

To notarize an explicitly approved release, configure `NOTARY_KEY_PATH`, `NOTARY_KEY_ID` and `NOTARY_ISSUER_ID` for an App Store Connect key, or `NOTARY_APPLE_ID`, `NOTARY_PASSWORD` and `NOTARY_TEAM_ID` for the Apple ID route. Then run:

```sh
native/tools/macos-notarize.sh native/build-universal/plugin release Release
```

This command uploads the bundles to Apple, staples accepted tickets, and creates archives. It is an optional release operation; it is not part of the ordinary build or test commands. Credentials are not stored in this repository. No remote publishing workflow has been configured.
