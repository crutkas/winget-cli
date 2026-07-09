// -----------------------------------------------------------------------------
// <copyright file="DSCv3PackagesResourceCommand.cs" company="Microsoft Corporation">
//     Copyright (c) Microsoft Corporation. Licensed under the MIT License.
// </copyright>
// -----------------------------------------------------------------------------

namespace AppInstallerCLIE2ETests
{
    using System.Collections.Generic;
    using System.Linq;
    using System.Text.Json.Serialization;
    using AppInstallerCLIE2ETests.Helpers;
    using NUnit.Framework;

    /// <summary>
    /// Tests for the `packages` DSC v3 resource, which manages a group array of packages in a single instance.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.SpacingRules", "SA1010:Opening square brackets should be spaced correctly", Justification = "https://github.com/DotNetAnalyzers/StyleCopAnalyzers/issues/3687 pending SC 1.2 release")]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.SpacingRules", "SA1011:Closing square brackets should be spaced correctly", Justification = "https://github.com/DotNetAnalyzers/StyleCopAnalyzers/issues/3687 pending SC 1.2 release")]
    public class DSCv3PackagesResourceCommand : DSCv3ResourceTestBase
    {
        private const string FirstPackageIdentifier = Constants.ExeInstallerPackageId;
        private const string SecondPackageIdentifier = Constants.PortableExePackageId;
        private const string PackagesResource = "packages";
        private const string PackagesPropertyName = "packages";

        /// <summary>
        /// Setup done once before all the tests here.
        /// </summary>
        [OneTimeSetUp]
        public void OneTimeSetup()
        {
            TestCommon.SetupTestSource();
            WinGetSettingsHelper.ConfigureLoggingLevel("verbose");
            EnsureTestResourcePresence();
        }

        /// <summary>
        /// Teardown done once after all the tests here.
        /// </summary>
        [OneTimeTearDown]
        public void OneTimeTeardown()
        {
            RemoveTestPackages();
            WinGetSettingsHelper.ConfigureLoggingLevel(null);
            TestCommon.TearDownTestSource();
        }

        /// <summary>
        /// Set up. Ensures a clean slate so an earlier failed run does not leave packages installed.
        /// </summary>
        [SetUp]
        public void Setup()
        {
            RemoveTestPackages();
        }

        /// <summary>
        /// Calls `get` on the `packages` resource when the members have mixed presence.
        /// Why: the group must aggregate per-package state, reporting each member's `_exist` independently
        /// rather than collapsing to a single value.
        /// </summary>
        [Test]
        public void Packages_Get_MixedPresence()
        {
            var setupInstall = TestCommon.RunAICLICommand("install", $"--id {FirstPackageIdentifier}");
            Assert.AreEqual(0, setupInstall.ExitCode);

            PackagesResourceData input = CreateInput(FirstPackageIdentifier, SecondPackageIdentifier);

            var result = RunDSCv3Command(PackagesResource, GetFunction, input);
            AssertSuccessfulResourceRun(ref result);

            PackagesResourceData output = GetSingleOutputLineAs<PackagesResourceData>(result.StdOut);
            Assert.IsNotNull(output);
            Assert.IsNotNull(output.Packages);
            Assert.AreEqual(2, output.Packages.Count);

            Assert.True(GetPackage(output, FirstPackageIdentifier).Exist);
            Assert.False(GetPackage(output, SecondPackageIdentifier).Exist);
        }

        /// <summary>
        /// Calls `test` on the `packages` resource when not all members are present.
        /// Why: a single missing package must put the whole group out of desired state; this guards the
        /// aggregate AND semantics and the diff shape returned for the group.
        /// </summary>
        [Test]
        public void Packages_Test_NotAllPresent()
        {
            var setupInstall = TestCommon.RunAICLICommand("install", $"--id {FirstPackageIdentifier}");
            Assert.AreEqual(0, setupInstall.ExitCode);

            PackagesResourceData input = CreateInput(FirstPackageIdentifier, SecondPackageIdentifier);

            var result = RunDSCv3Command(PackagesResource, TestFunction, input);
            AssertSuccessfulResourceRun(ref result);

            (PackagesResourceData output, List<string> diff) = GetSingleOutputLineAndDiffAs<PackagesResourceData>(result.StdOut);
            Assert.IsNotNull(output);
            Assert.False(output.InDesiredState);
            AssertDiffState(diff, [ PackagesPropertyName ]);
        }

        /// <summary>
        /// Calls `test` on the `packages` resource when all members are present.
        /// Why: guards the positive aggregate case so a fully-satisfied group reports in-desired-state with an empty diff.
        /// </summary>
        [Test]
        public void Packages_Test_AllPresent()
        {
            var firstInstall = TestCommon.RunAICLICommand("install", $"--id {FirstPackageIdentifier}");
            Assert.AreEqual(0, firstInstall.ExitCode);
            var secondInstall = TestCommon.RunAICLICommand("install", $"--id {SecondPackageIdentifier}");
            Assert.AreEqual(0, secondInstall.ExitCode);

            PackagesResourceData input = CreateInput(FirstPackageIdentifier, SecondPackageIdentifier);

            var result = RunDSCv3Command(PackagesResource, TestFunction, input);
            AssertSuccessfulResourceRun(ref result);

            (PackagesResourceData output, List<string> diff) = GetSingleOutputLineAndDiffAs<PackagesResourceData>(result.StdOut);
            Assert.IsNotNull(output);
            Assert.True(output.InDesiredState);
            AssertDiffState(diff, new List<string>());
        }

        /// <summary>
        /// Calls `set` on the `packages` resource to install all missing members.
        /// Why: this is the core reason the resource exists — a single instance installs the whole batch of app ids.
        /// </summary>
        [Test]
        public void Packages_Set_InstallsMissing()
        {
            PackagesResourceData input = CreateInput(FirstPackageIdentifier, SecondPackageIdentifier);

            var result = RunDSCv3Command(PackagesResource, SetFunction, input, 300000);
            AssertSuccessfulResourceRun(ref result);

            (PackagesResourceData output, List<string> diff) = GetSingleOutputLineAndDiffAs<PackagesResourceData>(result.StdOut);
            Assert.IsNotNull(output);
            Assert.AreEqual(2, output.Packages.Count);
            Assert.True(GetPackage(output, FirstPackageIdentifier).Exist);
            Assert.True(GetPackage(output, SecondPackageIdentifier).Exist);
            AssertDiffState(diff, [ PackagesPropertyName ]);

            // Confirm with a follow-up `get` that both packages are actually installed.
            var getResult = RunDSCv3Command(PackagesResource, GetFunction, input);
            AssertSuccessfulResourceRun(ref getResult);

            PackagesResourceData getOutput = GetSingleOutputLineAs<PackagesResourceData>(getResult.StdOut);
            Assert.True(GetPackage(getOutput, FirstPackageIdentifier).Exist);
            Assert.True(GetPackage(getOutput, SecondPackageIdentifier).Exist);
        }

        /// <summary>
        /// Calls `set` on the `packages` resource with a member requesting `_exist = false`.
        /// Why: the group must honor per-member exist overrides so a batch can add some packages while removing others.
        /// </summary>
        [Test]
        public void Packages_Set_RemovesWhenExistFalse()
        {
            var setupInstall = TestCommon.RunAICLICommand("install", $"--id {FirstPackageIdentifier}");
            Assert.AreEqual(0, setupInstall.ExitCode);

            PackagesResourceData input = new PackagesResourceData()
            {
                Packages = new List<PackageResourceData>()
                {
                    new PackageResourceData() { Identifier = FirstPackageIdentifier, Exist = false },
                    new PackageResourceData() { Identifier = SecondPackageIdentifier },
                },
            };

            var result = RunDSCv3Command(PackagesResource, SetFunction, input, 300000);
            AssertSuccessfulResourceRun(ref result);

            (PackagesResourceData output, List<string> diff) = GetSingleOutputLineAndDiffAs<PackagesResourceData>(result.StdOut);
            Assert.IsNotNull(output);
            Assert.False(GetPackage(output, FirstPackageIdentifier).Exist);
            Assert.True(GetPackage(output, SecondPackageIdentifier).Exist);
            AssertDiffState(diff, [ PackagesPropertyName ]);
        }

        /// <summary>
        /// Calls `set` on the `packages` resource when it is already in the desired state.
        /// Why: DSC set must be idempotent — a no-op set should report an empty diff and make no changes.
        /// </summary>
        [Test]
        public void Packages_Set_Idempotent()
        {
            var firstInstall = TestCommon.RunAICLICommand("install", $"--id {FirstPackageIdentifier}");
            Assert.AreEqual(0, firstInstall.ExitCode);
            var secondInstall = TestCommon.RunAICLICommand("install", $"--id {SecondPackageIdentifier}");
            Assert.AreEqual(0, secondInstall.ExitCode);

            PackagesResourceData input = CreateInput(FirstPackageIdentifier, SecondPackageIdentifier);

            var result = RunDSCv3Command(PackagesResource, SetFunction, input);
            AssertSuccessfulResourceRun(ref result);

            (PackagesResourceData output, List<string> diff) = GetSingleOutputLineAndDiffAs<PackagesResourceData>(result.StdOut);
            Assert.IsNotNull(output);
            Assert.True(GetPackage(output, FirstPackageIdentifier).Exist);
            Assert.True(GetPackage(output, SecondPackageIdentifier).Exist);
            AssertDiffState(diff, new List<string>());
        }

        /// <summary>
        /// Calls `export` on the `packages` resource.
        /// Why: the group export must emit a single instance whose `packages` array contains the installed packages,
        /// so a machine's state can be captured as one batch resource.
        /// </summary>
        [Test]
        public void Packages_Export()
        {
            var setupInstall = TestCommon.RunAICLICommand("install", $"--id {FirstPackageIdentifier}");
            Assert.AreEqual(0, setupInstall.ExitCode);

            var result = RunDSCv3Command(PackagesResource, ExportFunction, " ", 300000);
            AssertSuccessfulResourceRun(ref result);

            PackagesResourceData output = GetSingleOutputLineAs<PackagesResourceData>(result.StdOut);
            Assert.IsNotNull(output);
            Assert.IsNotNull(output.Packages);
            Assert.True(output.Packages.Any(p => p.Identifier == FirstPackageIdentifier));
        }

        private static PackagesResourceData CreateInput(params string[] identifiers)
        {
            return new PackagesResourceData()
            {
                Packages = identifiers.Select(id => new PackageResourceData() { Identifier = id }).ToList(),
            };
        }

        private static PackageResourceData GetPackage(PackagesResourceData data, string identifier)
        {
            PackageResourceData package = data.Packages.FirstOrDefault(p => p.Identifier == identifier);
            Assert.IsNotNull(package, $"Expected package `{identifier}` in output.");
            return package;
        }

        private static void RemoveTestPackages()
        {
            PackagesResourceData input = new PackagesResourceData()
            {
                Packages = new List<PackageResourceData>()
                {
                    new PackageResourceData() { Identifier = FirstPackageIdentifier, Exist = false },
                    new PackageResourceData() { Identifier = SecondPackageIdentifier, Exist = false },
                },
            };

            var result = RunDSCv3Command(PackagesResource, SetFunction, input);
            AssertSuccessfulResourceRun(ref result);
        }

        private class PackagesResourceData
        {
            [JsonPropertyName(InDesiredStatePropertyName)]
            public bool? InDesiredState { get; set; }

            [JsonPropertyName(PackagesPropertyName)]
            public List<PackageResourceData> Packages { get; set; }
        }

        private class PackageResourceData
        {
            [JsonPropertyName(ExistPropertyName)]
            public bool? Exist { get; set; }

            [JsonPropertyName(InDesiredStatePropertyName)]
            public bool? InDesiredState { get; set; }

            [JsonPropertyName("id")]
            public string Identifier { get; set; }

            public string Source { get; set; }

            public string Version { get; set; }

            public string MatchOption { get; set; }

            public bool? UseLatest { get; set; }

            public string InstallMode { get; set; }

            public bool? AcceptAgreements { get; set; }
        }
    }
}
