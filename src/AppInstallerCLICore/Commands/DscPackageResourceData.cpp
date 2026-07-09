// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "DscPackageResourceData.h"
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
    std::optional<MatchType> ToMatchType(const std::optional<std::string>& value)
    {
        if (!value)
        {
            return std::nullopt;
        }

        std::string lowerValue = Utility::ToLower(value.value());

        if (lowerValue == "equals")
        {
            return MatchType::Exact;
        }
        else if (lowerValue == "equals""case""insensitive")
        {
            return MatchType::CaseInsensitive;
        }
        else if (lowerValue == "starts""with""case""insensitive")
        {
            return MatchType::StartsWith;
        }
        else if (lowerValue == "contains""case""insensitive")
        {
            return MatchType::Substring;
        }

        THROW_HR(E_INVALIDARG);
    }

    PackageFunctionData::PackageFunctionData(Execution::Context& context, const std::optional<Json::Value>& json, bool ignoreFieldRequirements) :
        Input(json, ignoreFieldRequirements),
        ParentContext(context)
    {
        Reset();
    }

    void PackageFunctionData::Reset()
    {
        Output = Input.CopyForOutput();

        SubContext = ParentContext.CreateSubContext();
        SubContext->SetFlags(Execution::ContextFlag::DisableInteractivity);

        if (Input.AcceptAgreements().value_or(false))
        {
            SubContext->Args.AddArg(Execution::Args::Type::AcceptSourceAgreements);
            SubContext->Args.AddArg(Execution::Args::Type::AcceptPackageAgreements);
        }
    }

    void PackageFunctionData::PrepareSubContextInputs()
    {
        if (Input.Source())
        {
            SubContext->Args.AddArg(Execution::Args::Type::Source, Input.Source().value());
        }
    }

    bool PackageFunctionData::Get()
    {
        PrepareSubContextInputs();

        *SubContext <<
            Workflow::ReportExecutionStage(Workflow::ExecutionStage::Discovery) <<
            Workflow::OpenSource() <<
            Workflow::OpenCompositeSource(Workflow::DetermineInstalledSource(*SubContext), false, CompositeSearchBehavior::AllPackages);

        if (SubContext->IsTerminated())
        {
            ParentContext.Terminate(SubContext->GetTerminationHR());
            return false;
        }

        // Do a manual search of the now opened source
        Source& source = SubContext->Get<Execution::Data::Source>();
        MatchType matchType = ToMatchType(Input.MatchOption()).value_or(MatchType::CaseInsensitive);

        SearchRequest request;
        request.Filters.emplace_back(PackageMatchFilter(PackageMatchField::Id, matchType, Input.Identifier().value()));

        SearchResult result = source.Search(request);
        SubContext->Add<Execution::Data::SearchResult>(result);

        if (result.Matches.empty())
        {
            Output.Exist(false);
        }
        else if (result.Matches.size() > 1)
        {
            AICLI_LOG(Config, Warning, << "Found " << result.Matches.size() << " matches when searching for '" << Input.Identifier().value() << "'");
            Output.Exist(false);
        }
        else
        {
            auto& package = result.Matches.front().Package;
            SubContext->Add<Execution::Data::Package>(package);

            auto installedPackage = package->GetInstalled();

            // Fill Output and SubContext
            Output.Exist(static_cast<bool>(installedPackage));
            Output.Identifier(package->GetProperty(PackageProperty::Id));

            if (installedPackage)
            {
                auto versionKeys = installedPackage->GetVersionKeys();
                AICLI_LOG(CLI, Verbose, << "Package::Get found " << versionKeys.size() << " installed versions");

                std::shared_ptr<Repository::IPackageVersion> installedVersion;

                // Find the specific version provided if possible
                if (Input.Version())
                {
                    Utility::Version inputVersion{ Input.Version().value() };

                    for (const auto& key : versionKeys)
                    {
                        if (inputVersion == Utility::Version{ key.Version })
                        {
                            installedVersion = installedPackage->GetVersion(key);
                            break;
                        }
                    }
                }

                if (!installedVersion)
                {
                    installedVersion = installedPackage->GetLatestVersion();
                }

                if (installedVersion)
                {
                    Output.Version(installedVersion->GetProperty(PackageVersionProperty::Version));
                }

                auto data = Repository::GetLatestApplicableVersion(package);
                Output.UseLatest(!data.UpdateAvailable);
            }
        }

        AICLI_LOG(CLI, Verbose, << "Package::Get found:\n" << Json::writeString(Json::StreamWriterBuilder{}, Output.ToJson()));
        return true;
    }

    void PackageFunctionData::Uninstall()
    {
        AICLI_LOG(CLI, Verbose, << "Package::Uninstall invoked");

        if (Input.Version())
        {
            SubContext->Args.AddArg(Execution::Args::Type::TargetVersion, Input.Version().value());
        }
        else
        {
            SubContext->Args.AddArg(Execution::Args::Type::AllVersions);
        }

        *SubContext <<
            Workflow::UninstallSinglePackage;

        if (SubContext->IsTerminated())
        {
            ParentContext.Terminate(SubContext->GetTerminationHR());
            return;
        }

        Output.Exist(false);
        Output.Version(std::nullopt);
        Output.UseLatest(std::nullopt);
    }

    void PackageFunctionData::Install(bool allowDowngrade)
    {
        AICLI_LOG(CLI, Verbose, << "Package::Install invoked");

        if (Input.Version())
        {
            SubContext->Args.AddArg(Execution::Args::Type::Version, Input.Version().value());
        }

        *SubContext <<
            Workflow::SelectSinglePackageVersionForInstallOrUpgrade(Workflow::OperationType::Install, allowDowngrade) <<
            Workflow::InstallSinglePackage;

        if (SubContext->IsTerminated())
        {
            ParentContext.Terminate(SubContext->GetTerminationHR());
            return;
        }

        Output.Exist(true);

        Output.Version(std::nullopt);
        if (SubContext->Contains(Execution::Data::PackageVersion))
        {
            const auto& packageVersion = SubContext->Get<Execution::Data::PackageVersion>();
            if (packageVersion)
            {
                Output.Version(packageVersion->GetProperty(Repository::PackageVersionProperty::Version));
            }
        }

        Output.UseLatest(std::nullopt);
    }

    void PackageFunctionData::Reinstall()
    {
        AICLI_LOG(CLI, Verbose, << "Package::Reinstall invoked");

        SubContext->Args.AddArg(Execution::Args::Type::UninstallPrevious);

        Install(true);
    }

    bool PackageFunctionData::Test()
    {
        // Need to populate Output before calling
        THROW_HR_IF(E_UNEXPECTED, !Output.Exist().has_value());

        if (Input.ShouldExist())
        {
            if (Output.Exist().value())
            {
                AICLI_LOG(CLI, Verbose, << "Package::Test needed to inspect these properties: Version(" << TestVersion() << "), Latest(" << TestLatest() << ")");
                return TestVersion() && TestLatest();
            }
            else
            {
                AICLI_LOG(CLI, Verbose, << "Package::Test was false because the package was not installed");
                return false;
            }
        }
        else
        {
            AICLI_LOG(CLI, Verbose, << "Package::Test desired the package to not exist, and it " << (Output.Exist().value() ? "did" : "did not"));
            return !Output.Exist().value();
        }
    }

    Json::Value PackageFunctionData::DiffJson()
    {
        // Need to populate Output before calling
        THROW_HR_IF(E_UNEXPECTED, !Output.Exist().has_value());

        Json::Value result{ Json::ValueType::arrayValue };

        if (Input.ShouldExist() != Output.Exist().value())
        {
            result.append(std::string{ StandardExistProperty::Name() });
        }
        else
        {
            if (!TestVersion())
            {
                result.append(std::string{ VersionProperty::Name() });
            }

            if (!TestLatest())
            {
                result.append(std::string{ UseLatestProperty::Name() });
            }
        }

        return result;
    }

    bool PackageFunctionData::TestVersion()
    {
        if (Input.Version())
        {
            if (Output.Version())
            {
                return Utility::Version{ Input.Version().value() } == Utility::Version{ Output.Version().value() };
            }
            else
            {
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    bool PackageFunctionData::TestLatest()
    {
        if (Input.UseLatest() && Input.UseLatest().value())
        {
            if (Output.UseLatest())
            {
                return Output.UseLatest().value();
            }
            else
            {
                return false;
            }
        }
        else
        {
            return true;
        }
    }
}
