// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "DscPackageResource.h"
#include "DscPackageResourceData.h"
#include "DscComposableObject.h"
#include "Resources.h"
#include "Workflows/WorkflowBase.h"
#include "Workflows/ConfigurationFlow.h"
#include "Workflows/InstallFlow.h"
#include "Workflows/UninstallFlow.h"
#include "Workflows/UpdateFlow.h"
#include <winget/PackageVersionSelection.h>

using namespace AppInstaller::Utility::literals;
using namespace AppInstaller::Repository;

namespace AppInstaller::CLI
{
    DscPackageResource::DscPackageResource(std::string_view parent) :
        DscCommandBase(parent, "package", DscResourceKind::Resource,
            DscFunctions::Get | DscFunctions::Set | DscFunctions::Test | DscFunctions::Export | DscFunctions::Schema,
            DscFunctionModifiers::ImplementsPretest | DscFunctionModifiers::HandlesExist | DscFunctionModifiers::ReturnsStateAndDiff)
    {
    }

    Resource::LocString DscPackageResource::ShortDescription() const
    {
        return Resource::String::DscPackageResourceShortDescription;
    }

    Resource::LocString DscPackageResource::LongDescription() const
    {
        return Resource::String::DscPackageResourceLongDescription;
    }

    std::string DscPackageResource::ResourceType() const
    {
        return "Package";
    }

    void DscPackageResource::ResourceFunctionGet(Execution::Context& context) const
    {
        if (auto json = GetJsonFromInput(context))
        {
            PackageFunctionData data{ context, json };

            if (!data.Get())
            {
                return;
            }

            WriteJsonOutputLine(context, data.Output.ToJson());
        }
    }

    void DscPackageResource::ResourceFunctionSet(Execution::Context& context) const
    {
        if (auto json = GetJsonFromInput(context))
        {
            PackageFunctionData data{ context, json };

            if (!data.Get())
            {
                return;
            }

            // Capture the diff before updating the output
            auto diff = data.DiffJson();

            if (!data.Test())
            {
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

                if (data.SubContext->IsTerminated())
                {
                    return;
                }
            }

            WriteJsonOutputLine(context, data.Output.ToJson());
            WriteJsonOutputLine(context, diff);
        }
    }

    void DscPackageResource::ResourceFunctionTest(Execution::Context& context) const
    {
        if (auto json = GetJsonFromInput(context))
        {
            PackageFunctionData data{ context, json };

            if (!data.Get())
            {
                return;
            }

            data.Output.InDesiredState(data.Test());

            WriteJsonOutputLine(context, data.Output.ToJson());
            WriteJsonOutputLine(context, data.DiffJson());
        }
    }

    void DscPackageResource::ResourceFunctionExport(Execution::Context& context) const
    {
        auto json = GetJsonFromInput(context, false);
        PackageFunctionData data{ context, json, true };

        data.PrepareSubContextInputs();

        if (!data.Input.UseLatest().value_or(true))
        {
            data.SubContext->Args.AddArg(Execution::Args::Type::IncludeVersions);
        }

        data.SubContext->Args.AddArg(Execution::Args::Type::ConfigurationExportAll);

        *data.SubContext <<
            Workflow::SearchSourceForPackageExport;

        if (data.SubContext->IsTerminated())
        {
            context.Terminate(data.SubContext->GetTerminationHR());
            return;
        }

        const auto& packageCollection = data.SubContext->Get<Execution::Data::PackageCollection>();

        for (const auto& source : packageCollection.Sources)
        {
            for (const auto& package : source.Packages)
            {
                PackageResourceObject output;

                output.Identifier(package.Id);
                output.Source(source.Details.Name);

                if (!package.VersionAndChannel.GetVersion().IsEmpty())
                {
                    output.Version(package.VersionAndChannel.GetVersion().ToString());
                }

                // TODO: Exporting scope requires one or more of the following:
                //  1. Support for "OrUnknown" scope variants during Set (and workflows)
                //  2. Tracking scope intent as we do for some other installer properties
                //  3. Checking for the availability of the current scope in the package

                WriteJsonOutputLine(context, output.ToJson());
            }
        }
    }

    void DscPackageResource::ResourceFunctionSchema(Execution::Context& context) const
    {
        WriteJsonOutputLine(context, PackageResourceObject::Schema(ResourceType()));
    }
}
