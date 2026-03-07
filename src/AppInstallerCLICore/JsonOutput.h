// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "ExecutionContext.h"

#include <json/json.h>
#include <string>

namespace AppInstaller::CLI::Execution
{
    // Checks whether the current context requests JSON output.
    bool IsJsonOutputType(const Context& context);

    // Converts the output type argument value to a lowercase string for comparison.
    std::string GetOutputType(const Context& context);
}

namespace AppInstaller::CLI::Workflow
{
    // Outputs the search results in JSON format.
    // Required Args: None
    // Inputs: SearchResult
    // Outputs: None
    void ReportSearchResultJson(Execution::Context& context);

    // Outputs the list/upgrade results in JSON format.
    // Required Args: None
    // Inputs: SearchResult
    // Outputs: None
    void ReportListResultJson(Execution::Context& context, bool onlyShowUpgrades);

    // Outputs the show result (manifest info) in JSON format.
    // Required Args: None
    // Inputs: Manifest, Installer (optional)
    // Outputs: None
    void ReportManifestInfoJson(Execution::Context& context);
}
