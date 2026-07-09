// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "DscComposableObject.h"
#include "ExecutionContext.h"
#include "Resources.h"
#include <winget/RepositorySource.h>
#include <json/json.h>
#include <memory>
#include <optional>
#include <string>

namespace AppInstaller::CLI
{
    WINGET_DSC_DEFINE_COMPOSABLE_PROPERTY_FLAGS(IdProperty, std::string, Identifier, "id", DscComposablePropertyFlag::Required | DscComposablePropertyFlag::CopyToOutput, Resource::String::DscResourcePropertyDescriptionPackageId);
    WINGET_DSC_DEFINE_COMPOSABLE_PROPERTY_FLAGS(SourceProperty, std::string, Source, "source", DscComposablePropertyFlag::CopyToOutput, Resource::String::DscResourcePropertyDescriptionPackageSource);
    WINGET_DSC_DEFINE_COMPOSABLE_PROPERTY(VersionProperty, std::string, Version, "version", Resource::String::DscResourcePropertyDescriptionPackageVersion);
    WINGET_DSC_DEFINE_COMPOSABLE_PROPERTY_ENUM(MatchOptionProperty, std::string, MatchOption, "matchOption", Resource::String::DscResourcePropertyDescriptionPackageMatchOption, ({ "equals", "equalsCaseInsensitive", "startsWithCaseInsensitive", "containsCaseInsensitive" }), "equalsCaseInsensitive");
    WINGET_DSC_DEFINE_COMPOSABLE_PROPERTY_DEFAULT(UseLatestProperty, bool, UseLatest, "useLatest", Resource::String::DscResourcePropertyDescriptionPackageUseLatest, "false");
    WINGET_DSC_DEFINE_COMPOSABLE_PROPERTY_ENUM(InstallModeProperty, std::string, InstallMode, "installMode", Resource::String::DscResourcePropertyDescriptionPackageInstallMode, ({ "default", "silent", "interactive" }), "silent");
    WINGET_DSC_DEFINE_COMPOSABLE_PROPERTY(AcceptAgreementsProperty, bool, AcceptAgreements, "acceptAgreements", Resource::String::DscResourcePropertyDescriptionAcceptAgreements);

    // TODO: To support Scope on this resource:
    //  1. Change the installed source to pull in all package info for both scopes by default
    //  2. Change the installed source open in workflows to always open for everything, regardless of scope
    //  3. Improve correlation handling if needed for cross-scope package installations
    //  4. Update the test EXE installer to handle being installed for both scopes
    using PackageResourceObject = DscComposableObject<StandardExistProperty, StandardInDesiredStateProperty, IdProperty, SourceProperty, VersionProperty, MatchOptionProperty, UseLatestProperty, InstallModeProperty, AcceptAgreementsProperty>;

    // Converts the match option string value to a MatchType, or nullopt if not provided.
    std::optional<AppInstaller::Repository::MatchType> ToMatchType(const std::optional<std::string>& value);

    // Holds the state and operations for getting and setting a single package's state.
    // Shared by the `package` and `packages` DSC v3 resources.
    struct PackageFunctionData
    {
        PackageFunctionData(Execution::Context& context, const std::optional<Json::Value>& json, bool ignoreFieldRequirements = false);

        const PackageResourceObject Input;
        PackageResourceObject Output;
        Execution::Context& ParentContext;
        std::unique_ptr<Execution::Context> SubContext;

        // Reset the state that is modified by Get
        void Reset();

        void PrepareSubContextInputs();

        // Fills the Output object with the current state
        bool Get();

        void Uninstall();

        void Install(bool allowDowngrade = false);

        void Reinstall();

        // Determines if the current Output values match the Input values state.
        bool Test();

        Json::Value DiffJson();

        bool TestVersion();

        bool TestLatest();
    };
}
