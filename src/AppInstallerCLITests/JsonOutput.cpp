// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "WorkflowCommon.h"
#include <Commands/SearchCommand.h>
#include <Commands/ShowCommand.h>
#include <Commands/ListCommand.h>
#include <Commands/UpgradeCommand.h>
#include <Workflows/ShowFlow.h>
#include <Workflows/WorkflowBase.h>
#include <JsonOutput.h>

#include <json/json.h>

using namespace TestCommon;
using namespace AppInstaller::CLI;
using namespace AppInstaller::CLI::Execution;

namespace
{
    Json::Value ParseJsonOutput(const std::string& output)
    {
        // The output may contain non-JSON lines (warnings, progress).
        // Find the JSON object by looking for the first '{' and last '}'.
        auto start = output.find('{');
        auto end = output.rfind('}');
        REQUIRE(start != std::string::npos);
        REQUIRE(end != std::string::npos);
        REQUIRE(end > start);

        std::string jsonStr = output.substr(start, end - start + 1);

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(jsonStr);
        bool success = Json::parseFromStream(builder, stream, &root, &errors);
        INFO("JSON parse errors: " << errors);
        REQUIRE(success);
        return root;
    }
}

TEST_CASE("JsonOutput_SearchReturnsValidJson", "[JsonOutput][workflow]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto previousThreadGlobals = context.SetForCurrentThread();
    OverrideForOpenSource(context, CreateTestSource({ TSR::TestQuery_ReturnOne }));
    context.Args.AddArg(Execution::Args::Type::Query, TSR::TestQuery_ReturnOne.Query);
    context.Args.AddArg(Execution::Args::Type::OutputType, "json");

    SearchCommand search({});
    search.Execute(context);
    INFO(output.str());

    auto json = ParseJsonOutput(output.str());

    // Verify schema and metadata
    REQUIRE(json.isMember("$schema"));
    REQUIRE(json["$schema"].asString() == "https://aka.ms/winget-search.schema.1.0.json");
    REQUIRE(json.isMember("WinGetVersion"));
    REQUIRE(json["Command"].asString() == "search");

    // Verify results array exists
    REQUIRE(json.isMember("Results"));
    REQUIRE(json["Results"].isArray());
    REQUIRE(json["Results"].size() >= 1);

    // Verify result structure
    const auto& firstResult = json["Results"][0];
    REQUIRE(firstResult.isMember("Name"));
    REQUIRE(firstResult.isMember("Id"));
    REQUIRE(firstResult.isMember("Version"));
    REQUIRE(firstResult["Name"].isString());
    REQUIRE(firstResult["Id"].isString());
    REQUIRE(firstResult["Version"].isString());

    // Verify ResultCount matches array size
    REQUIRE(json.isMember("ResultCount"));
    REQUIRE(json["ResultCount"].asUInt() == json["Results"].size());

    // Verify Truncated field
    REQUIRE(json.isMember("Truncated"));
    REQUIRE(json["Truncated"].isBool());
}

TEST_CASE("JsonOutput_SearchMultipleResults", "[JsonOutput][workflow]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto previousThreadGlobals = context.SetForCurrentThread();
    OverrideForOpenSource(context, CreateTestSource({ TSR::TestQuery_ReturnTwo }));
    context.Args.AddArg(Execution::Args::Type::Query, TSR::TestQuery_ReturnTwo.Query);
    context.Args.AddArg(Execution::Args::Type::OutputType, "json");

    SearchCommand search({});
    search.Execute(context);
    INFO(output.str());

    auto json = ParseJsonOutput(output.str());

    REQUIRE(json["Results"].isArray());
    REQUIRE(json["Results"].size() == 2);
    REQUIRE(json["ResultCount"].asUInt() == 2);

    // Verify both results have required fields
    for (const auto& result : json["Results"])
    {
        REQUIRE(result.isMember("Name"));
        REQUIRE(result.isMember("Id"));
        REQUIRE(result.isMember("Version"));
    }
}

TEST_CASE("JsonOutput_SearchDefaultIsTable", "[JsonOutput][workflow]")
{
    // Without --output-type, output should be the normal table (not JSON)
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto previousThreadGlobals = context.SetForCurrentThread();
    OverrideForOpenSource(context, CreateTestSource({ TSR::TestQuery_ReturnOne }));
    context.Args.AddArg(Execution::Args::Type::Query, TSR::TestQuery_ReturnOne.Query);
    // No OutputType arg added

    SearchCommand search({});
    search.Execute(context);
    INFO(output.str());

    // Table output should NOT start with '{'
    std::string result = output.str();
    auto firstNonSpace = result.find_first_not_of(" \t\n\r");
    if (firstNonSpace != std::string::npos)
    {
        REQUIRE(result[firstNonSpace] != '{');
    }
}

TEST_CASE("JsonOutput_ShowReturnsValidJson", "[JsonOutput][workflow]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto previousThreadGlobals = context.SetForCurrentThread();
    OverrideForOpenSource(context, CreateTestSource({ TSR::TestQuery_ReturnOne }));
    context.Args.AddArg(Execution::Args::Type::Query, TSR::TestQuery_ReturnOne.Query);
    context.Args.AddArg(Execution::Args::Type::OutputType, "json");

    ShowCommand show({});
    show.Execute(context);
    INFO(output.str());

    auto json = ParseJsonOutput(output.str());

    // Verify schema and metadata
    REQUIRE(json.isMember("$schema"));
    REQUIRE(json["$schema"].asString() == "https://aka.ms/winget-show.schema.1.0.json");
    REQUIRE(json["Command"].asString() == "show");

    // Verify Package object
    REQUIRE(json.isMember("Package"));
    REQUIRE(json["Package"].isObject());

    const auto& pkg = json["Package"];
    REQUIRE(pkg.isMember("Name"));
    REQUIRE(pkg.isMember("Id"));
    REQUIRE(pkg.isMember("Version"));
    REQUIRE(pkg["Name"].isString());
    REQUIRE(pkg["Id"].isString());
    REQUIRE(pkg["Version"].isString());

    // Verify the actual package data matches the test fixture
    REQUIRE(pkg["Id"].asString() == "AppInstallerCliTest.TestExeInstaller");
    REQUIRE(pkg["Name"].asString() == "AppInstaller Test Exe Installer");
    REQUIRE(pkg["Version"].asString() == "1.0.0.0");
}

TEST_CASE("JsonOutput_ShowFromManifest", "[JsonOutput][workflow]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto previousThreadGlobals = context.SetForCurrentThread();
    context.Args.AddArg(Execution::Args::Type::Manifest, TestDataFile("InstallFlowTest_Exe.yaml").GetPath().u8string());
    context.Args.AddArg(Execution::Args::Type::OutputType, "json");

    ShowCommand show({});
    show.Execute(context);
    INFO(output.str());

    auto json = ParseJsonOutput(output.str());

    REQUIRE(json.isMember("Package"));
    REQUIRE(json["Package"].isMember("Name"));
    REQUIRE(json["Package"].isMember("Version"));

    // Installer info should be present when manifest has installers
    REQUIRE(json["Package"].isMember("Installer"));
    REQUIRE(json["Package"]["Installer"].isMember("Type"));
    REQUIRE(json["Package"]["Installer"].isMember("Url"));
    REQUIRE(json["Package"]["Installer"].isMember("SHA256"));
}

TEST_CASE("JsonOutput_ListReturnsValidJson", "[JsonOutput][workflow]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto previousThreadGlobals = context.SetForCurrentThread();
    OverrideForCompositeInstalledSource(context, CreateTestSource({ TSR::TestInstaller_Exe }));
    context.Args.AddArg(Execution::Args::Type::OutputType, "json");

    ListCommand list({});
    list.Execute(context);
    INFO(output.str());

    auto json = ParseJsonOutput(output.str());

    // Verify schema and metadata
    REQUIRE(json.isMember("$schema"));
    REQUIRE(json["$schema"].asString() == "https://aka.ms/winget-list.schema.1.0.json");
    REQUIRE(json["Command"].asString() == "list");

    // Verify results
    REQUIRE(json.isMember("Results"));
    REQUIRE(json["Results"].isArray());
    REQUIRE(json.isMember("ResultCount"));

    if (json["Results"].size() > 0)
    {
        const auto& first = json["Results"][0];
        REQUIRE(first.isMember("Name"));
        REQUIRE(first.isMember("Id"));
        REQUIRE(first.isMember("InstalledVersion"));
    }
}

TEST_CASE("JsonOutput_UpgradeListReturnsValidJson", "[JsonOutput][workflow]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto previousThreadGlobals = context.SetForCurrentThread();
    OverrideForCompositeInstalledSource(context, CreateTestSource({ TSR::TestInstaller_Exe }));
    context.Args.AddArg(Execution::Args::Type::OutputType, "json");
    // No --all or package query = list mode for upgrade

    UpgradeCommand upgrade({});
    upgrade.Execute(context);
    INFO(output.str());

    auto json = ParseJsonOutput(output.str());

    REQUIRE(json.isMember("$schema"));
    REQUIRE(json["Command"].asString() == "upgrade");
    REQUIRE(json.isMember("Results"));
    REQUIRE(json["Results"].isArray());
    REQUIRE(json.isMember("UpgradeCount"));
}

TEST_CASE("JsonOutput_IsJsonOutputType_True", "[JsonOutput][unit]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    context.Args.AddArg(Execution::Args::Type::OutputType, "json");

    REQUIRE(IsJsonOutputType(context));
}

TEST_CASE("JsonOutput_IsJsonOutputType_CaseInsensitive", "[JsonOutput][unit]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    context.Args.AddArg(Execution::Args::Type::OutputType, "JSON");

    REQUIRE(IsJsonOutputType(context));
}

TEST_CASE("JsonOutput_IsJsonOutputType_FalseWhenTable", "[JsonOutput][unit]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    context.Args.AddArg(Execution::Args::Type::OutputType, "table");

    REQUIRE_FALSE(IsJsonOutputType(context));
}

TEST_CASE("JsonOutput_IsJsonOutputType_FalseWhenAbsent", "[JsonOutput][unit]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    // No OutputType arg

    REQUIRE_FALSE(IsJsonOutputType(context));
}
