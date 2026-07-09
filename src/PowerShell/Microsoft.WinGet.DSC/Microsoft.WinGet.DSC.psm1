# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

using namespace System.Collections.Generic

# Check that we are running as an administrator
function Assert-IsAdministrator
{
    $windowsIdentity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $windowsPrincipal = New-Object -TypeName 'System.Security.Principal.WindowsPrincipal' -ArgumentList @( $windowsIdentity )

    $adminRole = [System.Security.Principal.WindowsBuiltInRole]::Administrator

    if (-not $windowsPrincipal.IsInRole($adminRole))
    {
        New-InvalidOperationException -Message "This resource must run as an Administrator."
    }
}

#region enums
enum WinGetAction
{
    Partial
    Full
}

enum WinGetEnsure
{
    Absent
    Present
}

enum WinGetMatchOption
{
    Equals
    EqualsCaseInsensitive
    StartsWithCaseInsensitive
    ContainsCaseInsensitive
}

enum WinGetInstallMode
{
    Default
    Silent
    Interactive
}

enum WinGetTrustLevel
{
    Undefined
    None
    Trusted
}

#endregion enums

#region DscResources
# Author here all DSC Resources.
# DSC Powershell doesn't support binary DSC resources without the MOF schema.
# DSC Powershell classes aren't discoverable if placed outside of the psm1.

# This resource is in charge of managing the settings.json file of winget.
[DSCResource()]
class WinGetUserSettings
{
    # We need a key. Do not set.
    [DscProperty(Key)]
    [string]$SID

    # A hash table with the desired settings.
    [DscProperty(Mandatory)]
    [Hashtable]$Settings

    [DscProperty()]
    [WinGetAction]$Action = [WinGetAction]::Full

    # Gets the current UserSettings by looking at the settings.json file for the current user.
    [WinGetUserSettings] Get()
    {
        $userSettings = Get-WinGetUserSetting
        $result = @{
            SID = ''
            Settings = $userSettings
        }
        return $result
    }

    # Tests if desired properties match.
    [bool] Test()
    {
        $hashArgs = @{
            UserSettings = $this.Settings
        }

        if ($this.Action -eq [WinGetAction]::Partial)
        {
            $hashArgs.Add('IgnoreNotSet', $true)
        }

        return Test-WinGetUserSetting @hashArgs
    }

    # Sets the desired properties.
    [void] Set()
    {
        $hashArgs = @{
            UserSettings = $this.Settings
        }

        if ($this.Action -eq [WinGetAction]::Partial)
        {
            $hashArgs.Add('Merge', $true)
        }

        Set-WinGetUserSetting @hashArgs
    }
}

# Handles configuration of administrator settings.
[DSCResource()]
class WinGetAdminSettings
{
    # We need a key. Do not set.
    [DscProperty(Key)]
    [string]$SID

    # A hash table with the desired admin settings.
    [DscProperty(Mandatory)]
    [Hashtable]$Settings

    # Gets the administrator settings.
    [WinGetAdminSettings] Get()
    {
        $settingsJson = Get-WinGetSetting
        # Get admin setting values.

        $result = @{
            SID = ''
            Settings = $settingsJson.adminSettings
        }
        return $result
    }

    # Tests if administrator settings given are set as expected.
    # This doesn't do a full comparison to allow users to don't have to update
    # their resource every time a new admin setting is added on winget.
    [bool] Test()
    {
        $adminSettings = $this.Get().Settings
        foreach ($adminSetting in $adminSettings.GetEnumerator())
        {
            if ($this.Settings.ContainsKey($adminSetting.Name))
            {
                if ($this.Settings[$adminSetting.Name] -ne $adminSetting.Value)
                {
                    return $false
                }
            }
        }

        return $true
    }

    # Sets the desired properties.
    [void] Set()
    {
        Assert-IsAdministrator

        # It might be better to implement an internal Test with one value, or
        # create a new instances with only one setting than calling Enable/Disable
        # for all of them even if only one is different.
        if (-not $this.Test())
        {
            foreach ($adminSetting in $this.Settings.GetEnumerator())
            {
                if ($adminSetting.Value)
                {
                    Enable-WinGetSetting -Name $adminSetting.Name
                }
                else
                {
                    Disable-WinGetSetting -Name $adminSetting.Name
                }
            }
        }
    }
}

[DSCResource()]
class WinGetSource
{
    [DscProperty(Key, Mandatory)]
    [string]$Name
    
    [DscProperty(Mandatory)]
    [string]$Argument
    
    [DscProperty()]
    [string]$Type
    
    [DscProperty()]
    [WinGetTrustLevel]$TrustLevel = [WinGetTrustLevel]::Undefined
    
    [DscProperty()]
    [nullable[bool]]$Explicit = $null
    
    [DscProperty()]
    [nullable[int]]$Priority = $null

    [DscProperty()]
    [WinGetEnsure]$Ensure = [WinGetEnsure]::Present

    [WinGetSource] Get()
    {
        if ([String]::IsNullOrWhiteSpace($this.Name))
        {
            throw "A value must be provided for WinGetSource::Name"
        }

        $currentSource = $null

        try {
            $currentSource = Get-WinGetSource -Name $this.Name
        }
        catch {
        }

        $result = [WinGetSource]::new()

        if ($currentSource)
        {
            $result.Ensure = [WinGetEnsure]::Present
            $result.Name = $currentSource.Name
            $result.Argument = $currentSource.Argument
            $result.Type = $currentSource.Type
            $result.TrustLevel = $currentSource.TrustLevel
            $result.Explicit = $currentSource.Explicit
            $result.Priority = $currentSource.Priority
        }
        else
        {
            $result.Ensure = [WinGetEnsure]::Absent
            $result.Name = $this.Name
        }

        return $result
    }

    [bool] Test()
    {
        return $this.TestAgainstCurrent($this.Get())
    }

    [void] Set()
    {
        Assert-IsAdministrator

        $currentSource = $this.Get()

        $removeSource = $false
        $resetSource = $false
        $addSource = $false

        if ($this.Ensure -eq [WinGetEnsure]::Present)
        {
            if ($currentSource.Ensure -eq [WinGetEnsure]::Present)
            {
                if (-not $this.TestAgainstCurrent($currentSource))
                {
                    $resetSource = $true
                    $addSource = $true
                }
                # else in desired state
            }
            else
            {
                $addSource = $true
            }
        }
        else
        {
            if ($currentSource.Ensure -eq [WinGetEnsure]::Present)
            {
                $removeSource = $true
            }
            # else in desired state (Absent)
        }

        if ($removeSource)
        {
            Remove-WinGetSource -Name $this.Name
        }
        # Only remove OR reset should be true, not both
        elseif ($resetSource)
        {
            Reset-WinGetSource -Name $this.Name
        }

        if ($addSource)
        {
            $hashArgs = @{
                Name = $this.Name
                Argument = $this.Argument
            }

            if (-not [string]::IsNullOrWhiteSpace($this.Type))
            {
                $hashArgs.Add("Type", $this.Type)
            }

            if ($this.TrustLevel -ne [WinGetTrustLevel]::Undefined)
            {
                $hashArgs.Add("TrustLevel", $this.TrustLevel)
            }

            if ($null -ne $this.Explicit)
            {
                $hashArgs.Add("Explicit", $this.Explicit)
            }

            if ($null -ne $this.Priority)
            {
                $hashArgs.Add("Priority", $this.Priority)
            }

            Add-WinGetSource @hashArgs
        }
    }
    
    # Test $this against a value retrieved from Get
    # We don't need to check Name because it is the Key for Get
    [bool] hidden TestAgainstCurrent([WinGetSource]$currentSource)
    {
        if ($this.Ensure -eq [WinGetEnsure]::Absent -and
            $currentSource.Ensure -eq [WinGetEnsure]::Absent)
        {
            return $true
        }

        if ($this.Ensure -ne $currentSource.Ensure -or
            $this.Argument -ne $currentSource.Argument)
        {
            return $false
        }

        if (-not([string]::IsNullOrWhiteSpace($this.Type)) -and
            $this.Type -ne $currentSource.Type)
        {
            return $false
        }

        if ($this.TrustLevel -ne [WinGetTrustLevel]::Undefined -and
            $this.TrustLevel -ne $currentSource.TrustLevel)
        {
            return $false
        }

        if ($null -ne $this.Explicit -and
            $this.Explicit -ne $currentSource.Explicit)
        {
            return $false
        }

        if ($null -ne $this.Priority -and
            $this.Priority -ne $currentSource.Priority)
        {
            return $false
        }

        return $true
    }
}

# TODO: It would be nice if these resource has a non configurable property that has extra information that comes from
# GitHub. We could implement it here or add more cmdlets in Microsoft.WinGet.Client.
[DSCResource()]
class WinGetPackageManager
{
    # We need a key. Do not set.
    [DscProperty(Key)]
    [string]$SID

    [DscProperty()]
    [string]$Version = ""

    [DscProperty()]
    [bool]$UseLatest

    [DscProperty()]
    [bool]$UseLatestPreRelease

    # If winget is not installed the version will be empty.
    [WinGetPackageManager] Get()
    {
        $integrityResource = [WinGetPackageManager]::new()
        if ($integrityResource.Test())
        {
            $integrityResource.Version = Get-WinGetVersion
        }

        return $integrityResource
    }

    # Tests winget is installed.
    [bool] Test()
    {
        try
        {
            $hashArgs = @{}

            if ($this.UseLatest)
            {
                $hashArgs.Add("Latest", $true)
            } elseif ($this.UseLatestPreRelease)
            {
                $hashArgs.Add("Latest", $true)
                $hashArgs.Add("IncludePrerelease", $true)
            } elseif (-not [string]::IsNullOrWhiteSpace($this.Version))
            {
                $hashArgs.Add("Version", $this.Version)
            }

            Assert-WinGetPackageManager @hashArgs
        }
        catch
        {
            return $false
        }

        return $true
    }

    # Repairs Winget.
    [void] Set()
    {
        if (-not $this.Test())
        {
            $result = -1
            $hashArgs = @{}

            if ($this.UseLatest)
            {
                $hashArgs.Add("Latest", $true)
            } elseif ($this.UseLatestPreRelease)
            {
                $hashArgs.Add("Latest", $true)
                $hashArgs.Add("IncludePrerelease", $true)
            } elseif (-not [string]::IsNullOrWhiteSpace($this.Version))
            {
                $hashArgs.Add("Version", $this.Version)
            }

            $result = Repair-WinGetPackageManager @hashArgs

            if ($result -ne 0)
            {
                # TODO: Localize.
                throw "Failed to repair winget. Result $result"
            }
        }
    }
}

[DSCResource()]
class WinGetPackage
{
    [DscProperty(Key, Mandatory)]
    [string]$Id

    [DscProperty(Key)]
    [string]$Source

    [DscProperty()]
    [string]$Version

    [DscProperty()]
    [WinGetEnsure]$Ensure = [WinGetEnsure]::Present

    [DscProperty()]
    [WinGetMatchOption]$MatchOption = [WinGetMatchOption]::EqualsCaseInsensitive

    [DscProperty()]
    [bool]$UseLatest = $false

    [DSCProperty()]
    [WinGetInstallMode]$InstallMode = [WinGetInstallMode]::Silent

    [PSObject] hidden $CatalogPackage = $null

    [WinGetPackage] Get()
    {
        if ([String]::IsNullOrWhiteSpace($this.Id))
        {
            throw "A value must be provided for WinGetPackage::Id"
        }

        $result = [WinGetPackage]::new()

        $hashArgs = @{
            Id = $this.Id
            MatchOption = $this.MatchOption
        }

        if (-not([string]::IsNullOrWhiteSpace($this.Source)))
        {
            $hashArgs.Add("Source", $this.Source)
        }

        $result.CatalogPackage = Get-WinGetPackage @hashArgs
        if ($null -ne $result.CatalogPackage)
        {
            $result.Ensure = [WinGetEnsure]::Present
            $result.Id = $result.CatalogPackage.Id
            $result.Source = $result.CatalogPackage.Source
            $result.Version = $result.CatalogPackage.InstalledVersion
            $result.UseLatest = -not $result.CatalogPackage.IsUpdateAvailable
        }
        else
        {
            $result.Ensure = [WinGetEnsure]::Absent
            $result.Id = $this.Id
            $result.MatchOption = $this.MatchOption
            $result.Source = $this.Source
        }

        return $result
    }

    [bool] Test()
    {
        return $this.TestAgainstCurrent($this.Get())
    }

    [void] Set()
    {
        $currentPackage = $this.Get()

        if (-not $this.TestAgainstCurrent($currentPackage))
        {
            $hashArgs = @{
                Id = $this.Id
                MatchOption = $this.MatchOption
                Mode = $this.InstallMode
            }
            
            if ($this.Ensure -eq [WinGetEnsure]::Present)
            {
                if (-not([string]::IsNullOrWhiteSpace($this.Source)))
                {
                    $hashArgs.Add("Source", $this.Source)
                }

                if ($currentPackage.Ensure -eq [WinGetEnsure]::Present)
                {
                    if ($this.UseLatest)
                    {
                        $this.TryUpdate($hashArgs)
                    }
                    elseif (-not([string]::IsNullOrWhiteSpace($this.Version)))
                    {
                        $hashArgs.Add("Version", $this.Version)

                        $compareResult = $currentPackage.CatalogPackage.CompareToVersion($this.Version)
                        switch ($compareResult)
                        {
                            'Lesser'
                            {
                                $this.TryUpdate($hashArgs)
                                break
                            }
                            {'Greater' -or 'Unknown'}
                            {
                                # The installed package has a greater version or unknown. Uninstall and install.
                                $this.Uninstall()
                                $this.Install($hashArgs)
                                break
                            }
                        }
                    }
                }
                else
                {
                    if (-not([string]::IsNullOrWhiteSpace($this.Version)))
                    {
                        $hashArgs.Add("Version", $this.Version)
                    }

                    $this.Install($hashArgs)
                }
            }
            else
            {
                $this.Uninstall()
            }
        }
    }
    
    [bool] hidden TestAgainstCurrent([WinGetPackage]$currentPackage)
    {
        if ($this.Ensure -eq [WinGetEnsure]::Absent -and
            $currentPackage.Ensure -eq [WinGetEnsure]::Absent)
        {
            return $true
        }

        $this.CatalogPackage = $currentPackage.CatalogPackage

        if ($this.Ensure -ne $currentPackage.Ensure)
        {
            return $false
        }

        # At this point we know is installed.
        # If asked for latest, but there are updates available.
        if ($this.UseLatest)
        {
            if (-not $currentPackage.UseLatest)
            {
                return $false
            }
        }
        # If there is an specific version, compare with the current installed version.
        elseif (-not ([string]::IsNullOrWhiteSpace($this.Version)))
        {
            $compareResult = $currentPackage.CatalogPackage.CompareToVersion($this.Version)
            if ($compareResult -ne 'Equal')
            {
                return $false
            }
        }

        return $true
    }

    hidden Install([Hashtable]$hashArgs)
    {
        $installResult = Install-WinGetPackage @hashArgs
        if (-not $installResult.Succeeded())
        {
            # TODO: Localize.
            throw "WinGetPackage Failed installing $($this.Id). $($installResult.ErrorMessage())"
        }
    }

    hidden Uninstall()
    {
        $uninstallResult = Uninstall-WinGetPackage -PSCatalogPackage $this.CatalogPackage
        if (-not $uninstallResult.Succeeded())
        {
            # TODO: Localize.
            throw "WinGetPackage Failed uninstalling $($this.Id). $($uninstallResult.ErrorMessage())"
        }
    }

    hidden Update([Hashtable]$hashArgs)
    {
        $updateResult = Update-WinGetPackage @hashArgs
        if (-not $updateResult.Succeeded())
        {
            # TODO: Localize.
            throw "WinGetPackage Failed updating $($this.Id). $($updateResult.ErrorMessage())"
        }
    }

    # Tries to update, if not, uninstall and install.
    hidden TryUpdate([Hashtable]$hashArgs)
    {
        try
        {
            $this.Update($hashArgs)
        }
        catch
        {
            $this.Uninstall()
            $this.Install($hashArgs)
        }
    }
}

# Describes a single package within a WinGetPackageSet. This is not a DSC resource on its own;
# it is an embedded instance type for the WinGetPackageSet 'Packages' property. Only 'Id' is
# required per item. Any property left unset (empty string or $null) inherits the corresponding
# set-level default from WinGetPackageSet, which is what lets a caller list many packages while
# only typing the shared settings once.
class WinGetPackageSetItem
{
    [DscProperty(Key, Mandatory)]
    [string]$Id

    [DscProperty()]
    [string]$Source

    [DscProperty()]
    [string]$Version

    [DscProperty()]
    [nullable[bool]]$UseLatest

    [DscProperty()]
    [nullable[WinGetInstallMode]]$InstallMode

    [DscProperty()]
    [nullable[WinGetEnsure]]$Ensure

    [DscProperty()]
    [nullable[WinGetMatchOption]]$MatchOption
}

# This resource manages a collection of packages in a single unit, applying shared defaults to
# every entry. It exists to reduce authoring boilerplate: instead of one WinGetPackage resource
# per application (each repeating Source/UseLatest/InstallMode), a caller declares the shared
# settings once and lists the packages by Id. Each entry is applied by delegating to the same
# per-package logic used by WinGetPackage, so install/update/uninstall behavior stays identical.
[DSCResource()]
class WinGetPackageSet
{
    # Identifies the set. It is the DSC key so multiple sets can coexist in one configuration.
    [DscProperty(Key)]
    [string]$SetName

    [DscProperty(Mandatory)]
    [WinGetPackageSetItem[]]$Packages

    # Set-level defaults. An individual package that leaves the matching property unset inherits
    # the value declared here.
    [DscProperty()]
    [string]$Source

    [DscProperty()]
    [string]$Version

    [DscProperty()]
    [bool]$UseLatest = $false

    [DscProperty()]
    [WinGetInstallMode]$InstallMode = [WinGetInstallMode]::Silent

    [DscProperty()]
    [WinGetEnsure]$Ensure = [WinGetEnsure]::Present

    [DscProperty()]
    [WinGetMatchOption]$MatchOption = [WinGetMatchOption]::EqualsCaseInsensitive

    [WinGetPackageSet] Get()
    {
        $result = [WinGetPackageSet]::new()
        $result.SetName = $this.SetName
        $result.Source = $this.Source
        $result.Version = $this.Version
        $result.UseLatest = $this.UseLatest
        $result.InstallMode = $this.InstallMode
        $result.Ensure = $this.Ensure
        $result.MatchOption = $this.MatchOption

        $currentItems = [List[WinGetPackageSetItem]]::new()
        foreach ($item in $this.Packages)
        {
            $package = $this.BuildPackage($item)
            $current = $package.Get()

            $currentItem = [WinGetPackageSetItem]::new()
            $currentItem.Id = $item.Id
            $currentItem.Source = $current.Source
            $currentItem.Version = $current.Version
            $currentItem.UseLatest = $current.UseLatest
            $currentItem.InstallMode = $package.InstallMode
            $currentItem.Ensure = $current.Ensure
            $currentItem.MatchOption = $package.MatchOption

            $currentItems.Add($currentItem)
        }

        $result.Packages = $currentItems.ToArray()
        return $result
    }

    [bool] Test()
    {
        foreach ($item in $this.Packages)
        {
            if (-not $this.BuildPackage($item).Test())
            {
                return $false
            }
        }

        return $true
    }

    [void] Set()
    {
        foreach ($item in $this.Packages)
        {
            $this.BuildPackage($item).Set()
        }
    }

    # Resolves a single set item against the set-level defaults and returns a fully configured
    # WinGetPackage. Item-level values win when present; otherwise the set default is used.
    [WinGetPackage] hidden BuildPackage([WinGetPackageSetItem]$item)
    {
        if ([string]::IsNullOrWhiteSpace($item.Id))
        {
            throw "A value must be provided for WinGetPackageSetItem::Id"
        }

        $package = [WinGetPackage]::new()
        $package.Id = $item.Id
        $package.Source = if (-not [string]::IsNullOrWhiteSpace($item.Source)) { $item.Source } else { $this.Source }
        $package.Version = if (-not [string]::IsNullOrWhiteSpace($item.Version)) { $item.Version } else { $this.Version }
        $package.UseLatest = if ($null -ne $item.UseLatest) { $item.UseLatest } else { $this.UseLatest }
        $package.InstallMode = if ($null -ne $item.InstallMode) { $item.InstallMode } else { $this.InstallMode }
        $package.Ensure = if ($null -ne $item.Ensure) { $item.Ensure } else { $this.Ensure }
        $package.MatchOption = if ($null -ne $item.MatchOption) { $item.MatchOption } else { $this.MatchOption }

        return $package
    }
}

#endregion DscResources
