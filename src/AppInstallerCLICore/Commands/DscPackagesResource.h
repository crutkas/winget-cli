// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "DscCommandBase.h"

namespace AppInstaller::CLI
{
    // A resource for managing the state of a group of packages in a single instance.
    struct DscPackagesResource : public DscCommandBase
    {
        DscPackagesResource(std::string_view parent);

        Resource::LocString ShortDescription() const override;
        Resource::LocString LongDescription() const override;

    protected:
        std::string ResourceType() const override;

        void ResourceFunctionGet(Execution::Context& context) const override;
        void ResourceFunctionSet(Execution::Context& context) const override;
        void ResourceFunctionTest(Execution::Context& context) const override;
        void ResourceFunctionExport(Execution::Context& context) const override;
        void ResourceFunctionSchema(Execution::Context& context) const override;
    };
}
