// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "JsonOutput.h"
#include "ExecutionContext.h"
#include "Workflows/WorkflowBase.h"
#include <winget/RepositorySearch.h>
#include <winget/PackageVersionSelection.h>
#include <winget/Manifest.h>
#include <winget/Runtime.h>
#include <AppInstallerSHA256.h>

#include <json/json.h>

using namespace AppInstaller::Repository;
using namespace std::string_literals;

namespace AppInstaller::CLI::Execution
{
    std::string GetOutputType(const Context& context)
    {
        std::string outputType;
        auto argValue = context.Args.GetArg(Args::Type::OutputType);
        if (!argValue.empty())
        {
            outputType = std::string(argValue);
            std::transform(outputType.begin(), outputType.end(), outputType.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
        return outputType;
    }

    bool IsJsonOutputType(const Context& context)
    {
        return context.Args.Contains(Args::Type::OutputType) && GetOutputType(context) == "json";
    }
}

namespace AppInstaller::CLI::Workflow
{
    namespace
    {
        std::string GetMatchFieldString(PackageMatchField field)
        {
            switch (field)
            {
            case PackageMatchField::Command: return "Command";
            case PackageMatchField::Id: return "Id";
            case PackageMatchField::Moniker: return "Moniker";
            case PackageMatchField::Name: return "Name";
            case PackageMatchField::Tag: return "Tag";
            case PackageMatchField::PackageFamilyName: return "PackageFamilyName";
            case PackageMatchField::ProductCode: return "ProductCode";
            case PackageMatchField::UpgradeCode: return "UpgradeCode";
            case PackageMatchField::NormalizedNameAndPublisher: return "NormalizedNameAndPublisher";
            case PackageMatchField::Market: return "Market";
            default: return "Unknown";
            }
        }

        std::string GetMatchTypeString(MatchType type)
        {
            switch (type)
            {
            case MatchType::CaseInsensitive: return "CaseInsensitive";
            case MatchType::Exact: return "Exact";
            case MatchType::StartsWith: return "StartsWith";
            case MatchType::Substring: return "Substring";
            case MatchType::Wildcard: return "Wildcard";
            case MatchType::Fuzzy: return "Fuzzy";
            case MatchType::FuzzySubstring: return "FuzzySubstring";
            default: return "Unknown";
            }
        }

        void WriteJsonToReporter(Execution::Context& context, const Json::Value& root)
        {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";
            auto out = context.Reporter.Info();
            out << Json::writeString(builder, root) << std::endl;
        }
    }

    void ReportSearchResultJson(Execution::Context& context)
    {
        auto& searchResult = context.Get<Execution::Data::SearchResult>();
        bool sourceIsComposite = context.Get<Execution::Data::Source>().IsComposite();

        Json::Value root(Json::objectValue);
        root["$schema"] = "https://aka.ms/winget-search.schema.1.0.json";
        root["WinGetVersion"] = std::string(Runtime::GetClientVersion());
        root["Command"] = "search";

        Json::Value results(Json::arrayValue);
        for (size_t i = 0; i < searchResult.Matches.size(); ++i)
        {
            auto latestVersion = GetAllAvailableVersions(searchResult.Matches[i].Package)->GetLatestVersion();

            Json::Value entry(Json::objectValue);
            entry["Name"] = static_cast<std::string>(latestVersion->GetProperty(PackageVersionProperty::Name));
            entry["Id"] = static_cast<std::string>(latestVersion->GetProperty(PackageVersionProperty::Id));
            entry["Version"] = static_cast<std::string>(latestVersion->GetProperty(PackageVersionProperty::Version));

            if (sourceIsComposite)
            {
                entry["Source"] = static_cast<std::string>(latestVersion->GetProperty(PackageVersionProperty::SourceName));
            }

            // Add match info
            const auto& match = searchResult.Matches[i];
            if (!match.MatchCriteria.empty())
            {
                Json::Value matchInfo(Json::objectValue);
                matchInfo["Field"] = GetMatchFieldString(match.MatchCriteria[0].Field);
                matchInfo["Type"] = GetMatchTypeString(match.MatchCriteria[0].Type);
                if (!match.MatchCriteria[0].Value.empty())
                {
                    matchInfo["Value"] = match.MatchCriteria[0].Value;
                }
                entry["Match"] = matchInfo;
            }

            results.append(entry);
        }

        root["Results"] = results;
        root["ResultCount"] = static_cast<Json::UInt>(searchResult.Matches.size());
        root["Truncated"] = searchResult.Truncated;

        WriteJsonToReporter(context, root);
    }

    void ReportListResultJson(Execution::Context& context, bool onlyShowUpgrades)
    {
        auto& searchResult = context.Get<Execution::Data::SearchResult>();

        Json::Value root(Json::objectValue);
        root["$schema"] = "https://aka.ms/winget-list.schema.1.0.json";
        root["WinGetVersion"] = std::string(Runtime::GetClientVersion());
        root["Command"] = onlyShowUpgrades ? "upgrade" : "list";

        Json::Value results(Json::arrayValue);
        int upgradeCount = 0;

        for (const auto& match : searchResult.Matches)
        {
            auto installedPackage = match.Package->GetInstalled();
            if (!installedPackage)
            {
                continue;
            }

            for (const auto& installedVersionKey : installedPackage->GetVersionKeys())
            {
                auto installedVersion = installedPackage->GetVersion(installedVersionKey);

                Json::Value entry(Json::objectValue);
                entry["Name"] = static_cast<std::string>(installedVersion->GetProperty(PackageVersionProperty::Name));
                entry["Id"] = static_cast<std::string>(installedVersion->GetProperty(PackageVersionProperty::Id));
                entry["InstalledVersion"] = static_cast<std::string>(installedVersion->GetProperty(PackageVersionProperty::Version));

                // Try to find available version
                auto availableVersions = GetAvailableVersionsForInstalledVersion(match.Package, installedVersion);
                if (availableVersions)
                {
                    auto latestAvailable = availableVersions->GetLatestVersion();
                    if (latestAvailable)
                    {
                        std::string availVer = static_cast<std::string>(latestAvailable->GetProperty(PackageVersionProperty::Version));
                        std::string installedVer = static_cast<std::string>(installedVersion->GetProperty(PackageVersionProperty::Version));
                        std::string sourceName = static_cast<std::string>(latestAvailable->GetProperty(PackageVersionProperty::SourceName));

                        bool isUpdate = !availVer.empty() && availVer != installedVer;
                        entry["AvailableVersion"] = availVer;
                        entry["UpgradeAvailable"] = isUpdate;
                        if (!sourceName.empty())
                        {
                            entry["Source"] = sourceName;
                        }

                        if (isUpdate)
                        {
                            upgradeCount++;
                        }
                    }
                }

                if (!onlyShowUpgrades || entry.get("UpgradeAvailable", false).asBool())
                {
                    results.append(entry);
                }

                break; // Only first installed version
            }
        }

        root["Results"] = results;
        root["ResultCount"] = static_cast<Json::UInt>(results.size());

        if (onlyShowUpgrades)
        {
            root["UpgradeCount"] = upgradeCount;
        }

        WriteJsonToReporter(context, root);
    }

    void ReportManifestInfoJson(Execution::Context& context)
    {
        const auto& manifest = context.Get<Execution::Data::Manifest>();

        Json::Value root(Json::objectValue);
        root["$schema"] = "https://aka.ms/winget-show.schema.1.0.json";
        root["WinGetVersion"] = std::string(Runtime::GetClientVersion());
        root["Command"] = "show";

        Json::Value pkg(Json::objectValue);
        pkg["Name"] = manifest.CurrentLocalization.Get<Manifest::Localization::PackageName>();
        pkg["Id"] = manifest.Id;
        pkg["Version"] = manifest.Version;
        pkg["Publisher"] = manifest.CurrentLocalization.Get<Manifest::Localization::Publisher>();

        auto publisherUrl = manifest.CurrentLocalization.Get<Manifest::Localization::PublisherUrl>();
        if (!publisherUrl.empty()) pkg["PublisherUrl"] = publisherUrl;

        auto publisherSupportUrl = manifest.CurrentLocalization.Get<Manifest::Localization::PublisherSupportUrl>();
        if (!publisherSupportUrl.empty()) pkg["PublisherSupportUrl"] = publisherSupportUrl;

        auto author = manifest.CurrentLocalization.Get<Manifest::Localization::Author>();
        if (!author.empty()) pkg["Author"] = author;

        pkg["Moniker"] = manifest.Moniker;

        auto description = manifest.CurrentLocalization.Get<Manifest::Localization::Description>();
        if (description.empty())
        {
            description = manifest.CurrentLocalization.Get<Manifest::Localization::ShortDescription>();
        }
        if (!description.empty()) pkg["Description"] = description;

        auto homepage = manifest.CurrentLocalization.Get<Manifest::Localization::PackageUrl>();
        if (!homepage.empty()) pkg["Homepage"] = homepage;

        auto license = manifest.CurrentLocalization.Get<Manifest::Localization::License>();
        if (!license.empty()) pkg["License"] = license;

        auto licenseUrl = manifest.CurrentLocalization.Get<Manifest::Localization::LicenseUrl>();
        if (!licenseUrl.empty()) pkg["LicenseUrl"] = licenseUrl;

        auto copyright = manifest.CurrentLocalization.Get<Manifest::Localization::Copyright>();
        if (!copyright.empty()) pkg["Copyright"] = copyright;

        auto copyrightUrl = manifest.CurrentLocalization.Get<Manifest::Localization::CopyrightUrl>();
        if (!copyrightUrl.empty()) pkg["CopyrightUrl"] = copyrightUrl;

        auto privacyUrl = manifest.CurrentLocalization.Get<Manifest::Localization::PrivacyUrl>();
        if (!privacyUrl.empty()) pkg["PrivacyUrl"] = privacyUrl;

        auto releaseNotes = manifest.CurrentLocalization.Get<Manifest::Localization::ReleaseNotes>();
        if (!releaseNotes.empty()) pkg["ReleaseNotes"] = releaseNotes;

        auto releaseNotesUrl = manifest.CurrentLocalization.Get<Manifest::Localization::ReleaseNotesUrl>();
        if (!releaseNotesUrl.empty()) pkg["ReleaseNotesUrl"] = releaseNotesUrl;

        // Tags
        auto tags = manifest.CurrentLocalization.Get<Manifest::Localization::Tags>();
        if (!tags.empty())
        {
            Json::Value tagsArray(Json::arrayValue);
            for (const auto& tag : tags)
            {
                tagsArray.append(tag);
            }
            pkg["Tags"] = tagsArray;
        }

        // Installer info (if available)
        if (context.Contains(Execution::Data::Installer))
        {
            const auto& installer = context.Get<Execution::Data::Installer>();
            if (installer)
            {
                Json::Value installerInfo(Json::objectValue);
                installerInfo["Type"] = Manifest::InstallerTypeToString(installer->BaseInstallerType);
                installerInfo["Url"] = installer->Url;
                installerInfo["SHA256"] = Utility::SHA256::ConvertToString(installer->Sha256);
                if (!installer->Locale.empty()) installerInfo["Locale"] = installer->Locale;
                pkg["Installer"] = installerInfo;
            }
        }

        root["Package"] = pkg;
        WriteJsonToReporter(context, root);
    }
}
