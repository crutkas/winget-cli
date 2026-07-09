---
author: WinGet Team
created on: 2026-07-09
last updated: 2026-07-09
---

# Batch package management DSC v3 resource (`Microsoft.WinGet/Packages`)

## Abstract

The `Microsoft.WinGet/Package` DSC v3 resource manages exactly one package per resource
instance. Managing many packages therefore requires one resource block per package, which is
verbose for the common "install this list of applications" scenario.

This spec describes `Microsoft.WinGet/Packages`, a group-style resource that manages an array of
packages in a single instance. The array elements use the same schema as `Microsoft.WinGet/Package`,
so anything expressible for a single package (source, version, match option, install mode, accept
agreements, and per-item `_exist`) is also expressible for each element of the batch.

## Inspiration

Users frequently want to describe machine state as a simple list of application ids and have WinGet
converge the machine to that list. Doing that today requires repeating a resource block for every
package. A single resource that accepts an array of packages makes batch installation concise and is
idiomatic for DSC v3.

## Solution design

A new native DSC v3 resource is added to `winget-cli`, exposed through `winget dsc packages` and
surfaced to DSC as `Microsoft.WinGet/Packages` (`Microsoft.WinGet.Dev/Packages` in dev builds).

The per-package logic (`get`, `test`, install/uninstall/upgrade decisions) is shared with the
existing `Package` resource by extracting it into `DscPackageResourceData.{h,cpp}`. The new resource
in `DscPackagesResource.{h,cpp}` iterates the array and delegates each element to that shared logic,
guaranteeing identical single-package behavior.

### Schema

```jsonc
{
  "packages": [                // required, array of package objects
    {
      "id": "Publisher.App",   // required
      "source": "winget",      // optional
      "version": "1.2.3",      // optional
      "matchOption": "equalsCaseInsensitive", // optional
      "useLatest": true,        // optional
      "installMode": "silent",  // optional
      "acceptAgreements": true,  // optional
      "_exist": true             // optional; set false to uninstall this member
    }
  ],
  "_inDesiredState": true       // output only; true when every member is in its desired state
}
```

### Function semantics

- **get** — Returns `{ "packages": [ <per-package get output> ] }`, preserving input order. Each
  element reports its own `_exist`, `version`, and `useLatest`.
- **test** — Returns the group output with `_inDesiredState` set to the logical AND of every member's
  test result. The diff is `["packages"]` when any member is not in its desired state, otherwise empty.
- **set** — Brings every member to its desired state (installs, upgrades, downgrades, or uninstalls as
  needed), skipping members that are already correct. Consistent with the single-package resource,
  processing is fail-fast: if applying a member terminates (for example, an unknown id), the resource
  stops and reports the failure. The diff is `["packages"]` when any change was required.
- **export** — Emits a single group instance whose `packages` array contains every installed package.
- **schema** — Emits the group schema; the `packages` array items reference the `Package` resource schema.

## UI/UX design

No interactive UI. The resource is consumed through configuration documents, for example:

```yaml
$schema: https://raw.githubusercontent.com/PowerShell/DSC/main/schemas/2023/08/config/document.json
resources:
  - name: Developer tools
    type: Microsoft.WinGet/Packages
    properties:
      packages:
        - id: Microsoft.PowerShell
        - id: Microsoft.WindowsTerminal
        - id: Git.Git
```

## Capabilities

### Accessibility

No impact; command-line and configuration-document driven.

### Security

No new privileges. Each member is installed through the existing install/uninstall workflows and
honors the same source and package agreement handling as the single-package resource.

### Reliability

Set is idempotent: members already in the desired state are not modified. Failures are surfaced as a
terminated resource run rather than being silently swallowed.

### Compatibility

Additive. The existing `Microsoft.WinGet/Package` resource is unchanged; its logic is merely shared.

### Performance, Power, and Efficiency

The source is opened per member (mirroring the single-package resource). This keeps the implementation
simple and correct; batching source opens is a possible future optimization.

## Potential issues

- Fail-fast set means a later member is not attempted if an earlier member fails. A future
  "continue on error" option could be added if requested.
- Source opens are not shared across members yet.

## Future considerations

- Optional continue-on-error behavior for `set`.
- Resource-level defaults (for example a default `source`) inherited by members.
- Sharing a single opened source across all members for performance.

## Resources

- [WinGet DSC resources](https://aka.ms/winget-dsc-resources)
- [PowerShell DSC v3](https://learn.microsoft.com/powershell/dsc/overview)
