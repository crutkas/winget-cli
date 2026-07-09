// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "DscPackagesResource.h"
#include "DscPackageResourceData.h"
#include "DscComposableObject.h"
#include "Resources.h"
#include "Workflows/WorkflowBase.h"

using namespace AppInstaller::Utility::literals;
using namespace AppInstaller::Repository;

namespace AppInstaller::CLI
{
    namespace
    {
        constexpr std::string_view s_PackagesPropertyName = "packages"sv;

        // Extracts the `packages` array from the group input.
        // Throws if the property is missing (when required) or not an array.
        std::optional<Json::Value> GetPackagesArray(const std::optional<Json::Value>& input, bool required)
        {
            if (!input)
            {
                THROW_HR_IF(WINGET_CONFIG_ERROR_MISSING_FIELD, required);
                return std::nullopt;
            }

            const Json::Value* packages = details::GetProperty(input.value(), s_PackagesPropertyName);
            if (!packages || packages->isNull())
            {
                THROW_HR_IF(WINGET_CONFIG_ERROR_MISSING_FIELD, required);
                return std::nullopt;
            }

            THROW_HR_IF(WINGET_CONFIG_ERROR_INVALID_FIELD_TYPE, !packages->isArray());
            return *packages;
        }

        // Creates the group output object, wrapping the per-package outputs in the `packages` array.
        Json::Value CreateGroupOutput(const std::vector<Json::Value>& packageOutputs, std::optional<bool> inDesiredState = std::nullopt)
        {
            Json::Value result{ Json::ValueType::objectValue };

            Json::Value packagesArray{ Json::ValueType::arrayValue };
            for (const auto& packageOutput : packageOutputs)
            {
                packagesArray.append(packageOutput);
            }

            result[std::string{ s_PackagesPropertyName }] = std::move(packagesArray);

            if (inDesiredState.has_value())
            {
                result[std::string{ StandardInDesiredStateProperty::Name() }] = inDesiredState.value();
            }

            return result;
        }

        // The diff for the group is expressed against the single `packages` property, as all of the
        // per-package state lives underneath it.
        Json::Value CreateGroupDiff(bool inDesiredState)
        {
            Json::Value result{ Json::ValueType::arrayValue };

            if (!inDesiredState)
            {
                result.append(std::string{ s_PackagesPropertyName });
            }

            return result;
        }

        // Applies the desired state for a single package, mirroring the logic used by the `package` resource.
        void SetSinglePackage(PackageFunctionData& data)
        {
            if (data.Test())
            {
                return;
            }

            if (data.Input.ShouldExist())
            {
                if (data.Output.Exist().value())
                {
                    if (!data.TestLatest())
                    {
                        // Install will swap to update flow
                        AICLI_LOG(CLI, Info, << "Installing package to update to latest");
                        data.Install();
                    }
                    else // (!data.TestVersion())
                    {
                        Utility::Version inputVersion{ data.Input.Version().value() };
                        Utility::Version outputVersion{ data.Output.Version().value() };

                        if (outputVersion < inputVersion)
                        {
                            // Install will swap to update flow
                            AICLI_LOG(CLI, Info, << "Installing package to update to desired version");
                            data.Install();
                        }
                        else
                        {
                            AICLI_LOG(CLI, Info, << "Reinstalling package to downgrade to desired version");
                            data.Reinstall();
                        }
                    }
                }
                else
                {
                    AICLI_LOG(CLI, Info, << "Installing package as it was not found");
                    data.Install();
                }
            }
            else
            {
                AICLI_LOG(CLI, Info, << "Uninstalling package as desired");
                data.Uninstall();
            }
        }
    }

    DscPackagesResource::DscPackagesResource(std::string_view parent) :
        DscCommandBase(parent, "packages", DscResourceKind::Resource,
            DscFunctions::Get | DscFunctions::Set | DscFunctions::Test | DscFunctions::Export | DscFunctions::Schema,
            DscFunctionModifiers::ImplementsPretest | DscFunctionModifiers::HandlesExist | DscFunctionModifiers::ReturnsStateAndDiff)
    {
    }

    Resource::LocString DscPackagesResource::ShortDescription() const
    {
        return Resource::String::DscPackagesResourceShortDescription;
    }

    Resource::LocString DscPackagesResource::LongDescription() const
    {
        return Resource::String::DscPackagesResourceLongDescription;
    }

    std::string DscPackagesResource::ResourceType() const
    {
        return "Packages";
    }

    void DscPackagesResource::ResourceFunctionGet(Execution::Context& context) const
    {
        if (auto json = GetJsonFromInput(context))
        {
            auto packages = GetPackagesArray(json, true);

            std::vector<Json::Value> packageOutputs;

            for (const auto& package : packages.value())
            {
                PackageFunctionData data{ context, package };

                if (!data.Get())
                {
                    return;
                }

                packageOutputs.emplace_back(data.Output.ToJson());
            }

            WriteJsonOutputLine(context, CreateGroupOutput(packageOutputs));
        }
    }

    void DscPackagesResource::ResourceFunctionSet(Execution::Context& context) const
    {
        if (auto json = GetJsonFromInput(context))
        {
            auto packages = GetPackagesArray(json, true);

            std::vector<Json::Value> packageOutputs;
            bool inDesiredState = true;

            for (const auto& package : packages.value())
            {
                PackageFunctionData data{ context, package };

                if (!data.Get())
                {
                    return;
                }

                // Capture whether the package needs to change before applying the desired state.
                if (!data.Test())
                {
                    inDesiredState = false;
                }

                SetSinglePackage(data);

                if (data.SubContext->IsTerminated())
                {
                    return;
                }

                packageOutputs.emplace_back(data.Output.ToJson());
            }

            WriteJsonOutputLine(context, CreateGroupOutput(packageOutputs));
            WriteJsonOutputLine(context, CreateGroupDiff(inDesiredState));
        }
    }

    void DscPackagesResource::ResourceFunctionTest(Execution::Context& context) const
    {
        if (auto json = GetJsonFromInput(context))
        {
            auto packages = GetPackagesArray(json, true);

            std::vector<Json::Value> packageOutputs;
            bool inDesiredState = true;

            for (const auto& package : packages.value())
            {
                PackageFunctionData data{ context, package };

                if (!data.Get())
                {
                    return;
                }

                bool packageInDesiredState = data.Test();
                inDesiredState = inDesiredState && packageInDesiredState;

                data.Output.InDesiredState(packageInDesiredState);
                packageOutputs.emplace_back(data.Output.ToJson());
            }

            WriteJsonOutputLine(context, CreateGroupOutput(packageOutputs, inDesiredState));
            WriteJsonOutputLine(context, CreateGroupDiff(inDesiredState));
        }
    }

    void DscPackagesResource::ResourceFunctionExport(Execution::Context& context) const
    {
        // Export produces a single group instance containing every installed package.
        PackageFunctionData data{ context, std::nullopt, true };

        data.PrepareSubContextInputs();

        data.SubContext->Args.AddArg(Execution::Args::Type::ConfigurationExportAll);

        *data.SubContext <<
            Workflow::SearchSourceForPackageExport;

        if (data.SubContext->IsTerminated())
        {
            context.Terminate(data.SubContext->GetTerminationHR());
            return;
        }

        const auto& packageCollection = data.SubContext->Get<Execution::Data::PackageCollection>();

        std::vector<Json::Value> packageOutputs;

        for (const auto& source : packageCollection.Sources)
        {
            for (const auto& package : source.Packages)
            {
                PackageResourceObject output;

                output.Identifier(package.Id);
                output.Source(source.Details.Name);

                packageOutputs.emplace_back(output.ToJson());
            }
        }

        WriteJsonOutputLine(context, CreateGroupOutput(packageOutputs));
    }

    void DscPackagesResource::ResourceFunctionSchema(Execution::Context& context) const
    {
        Json::Value result = details::GetBaseSchema(ResourceType());

        // The `packages` property is an array whose items match the `package` resource schema.
        details::AddPropertySchema(
            result,
            s_PackagesPropertyName,
            DscComposablePropertyFlag::Required,
            Json::ValueType::arrayValue,
            Resource::LocString{ Resource::String::DscResourcePropertyDescriptionPackages },
            {},
            {});

        result["properties"][std::string{ s_PackagesPropertyName }]["items"] = PackageResourceObject::Schema("WinGetPackageResourceObject");

        details::AddPropertySchema(
            result,
            StandardInDesiredStateProperty::Name(),
            DscComposablePropertyFlag::None,
            Json::ValueType::booleanValue,
            Resource::LocString{ Resource::String::DscResourcePropertyDescriptionInDesiredState },
            {},
            {});

        WriteJsonOutputLine(context, result);
    }
}
