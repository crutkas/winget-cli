---
external help file: Microsoft.WinGet.DSC.psm1-Help.xml
Module Name: Microsoft.WinGet.DSC
ms.date: 07/09/2026
online version:
schema: 2.0.0
title: WinGetPackageSet
---

# WinGetPackageSet

## SYNOPSIS
Configures multiple packages through WinGet using a single resource with shared defaults.

## DESCRIPTION

Allows a collection of WinGet packages to be configured in one unit. Settings that are common to
every package (such as `Source`, `UseLatest`, and `InstallMode`) are declared once at the set
level, and each package only needs to specify its `Id`. This reduces the boilerplate of declaring a
separate `WinGetPackage` resource for each application.

Any set-level property can be overridden on an individual package. A package property that is left
unset inherits the corresponding set-level default. Each package is applied using the same logic as
the `WinGetPackage` resource.

Because the whole collection is a single resource, per-package ordering (`dependsOn`) is not
available between items in the set. Packages that require ordering relative to one another should be
placed in separate sets or `WinGetPackage` resources.

## PARAMETERS

**Parameter**|**Attribute**|**DataType**|**Description**|**Allowed Values**
:-----|:-----|:-----|:-----|:-----
`SetName`|Key|String|A name that identifies the set within the configuration.|Any string
`Packages`|Mandatory|WinGetPackageSetItem[]|The list of packages to configure. Each entry requires an `Id`; other properties are optional and inherit the set-level default when unset.|See the `WinGetPackageSetItem` properties below
`Source`|Optional|String|Default source for every package that does not specify its own `Source`.|Use the `WinGetSource` resource to configure a source or `Get-WinGetSource` to discover the default sources
`Version`|Optional|String|Default version for every package that does not specify its own `Version`.|See the `AvailableVersions` property of output from `Find-WinGetPackage`
`Ensure`|Optional|WinGetEnsure|Default for whether packages should be installed (`Present`) or not (`Absent`).|`Present` (default), `Absent`
`MatchOption`|Optional|WinGetMatchOption|Default method used to compare the `Id` with available packages.|`Equals`, `EqualsCaseInsensitive` (default), `StartsWithCaseInsensitive`, `ContainsCaseInsensitive`
`UseLatest`|Optional|Boolean|Default for whether packages should be updated to the latest available version. If true, takes precedence over `Version`.|`True`, `False` (default)
`InstallMode`|Optional|WinGetInstallMode|Default interactivity level requested when installing.|`Default`, `Silent` (default), `Interactive`

### WinGetPackageSetItem

Each entry in `Packages` supports the following properties. Only `Id` is required; every other
property overrides the matching set-level default when specified.

**Parameter**|**Attribute**|**DataType**|**Description**|**Allowed Values**
:-----|:-----|:-----|:-----|:-----
`Id`|Key, Mandatory|String|The identifier of a WinGet package.|Use `Find-WinGetPackage` to search for packages
`Source`|Optional|String|Overrides the set-level `Source` for this package.|As above
`Version`|Optional|String|Overrides the set-level `Version` for this package.|As above
`Ensure`|Optional|WinGetEnsure|Overrides the set-level `Ensure` for this package.|`Present`, `Absent`
`MatchOption`|Optional|WinGetMatchOption|Overrides the set-level `MatchOption` for this package.|As above
`UseLatest`|Optional|Boolean|Overrides the set-level `UseLatest` for this package.|`True`, `False`
`InstallMode`|Optional|WinGetInstallMode|Overrides the set-level `InstallMode` for this package.|`Default`, `Silent`, `Interactive`
