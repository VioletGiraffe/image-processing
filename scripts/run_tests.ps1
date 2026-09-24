# Builds the tests and runs them, or with --benchmark runs the benchmarks instead and prints the ratio report.
# Exit code: 1 on a build or test failure, 2 when the environment is incomplete.
# Windows PowerShell 5.1 is enough; run_tests.bat starts this script, the execution policy being what it is.
# Options, before any other argument, the same as run_tests.sh:
#   --benchmark - benchmarks instead of tests
#   --debug     - the debug configuration; the default is release
#   --no-build  - runs what is already built
# The remaining arguments go to the Catch2 runner: a test spec narrows either mode, options such as --benchmark-samples 50 override the defaults.
# No param block: the parameter binder would claim Catch2's options, e.g. --warn as an abbreviation of -WarningAction.
#
# The Qt kit comes from QT_ROOT_DIR, or from a git-ignored local-env.ps1 beside this script.
# qmake and MSBuild need the MSVC environment: qmake probes cl for the compiler version it writes into the project.

$Configuration = 'release'
$Benchmark = $false
$NoBuild = $false
$optionCount = 0
foreach ($argument in $args)
{
	if ($argument -eq '--benchmark') { $Benchmark = $true }
	elseif ($argument -eq '--debug') { $Configuration = 'debug' }
	elseif ($argument -eq '--no-build') { $NoBuild = $true }
	else { break }
	++$optionCount
}
# Typed: a one-element array unrolls to a string, and splatting a string passes its characters one argument each
[string[]]$CatchArguments = @($args | Select-Object -Skip $optionCount)

function Fail-Environment([string]$message)
{
	[Console]::Error.WriteLine($message)
	exit 2
}

$testsDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'tests'

# The Qt kit, the directory holding bin\qmake.exe. Its bin goes on PATH: the test executable needs the Qt DLLs.
if (-not $env:QT_ROOT_DIR)
{
	$localEnvironment = Join-Path $PSScriptRoot 'local-env.ps1'
	if (Test-Path $localEnvironment) { . $localEnvironment }
}
if (-not $env:QT_ROOT_DIR)
{
	Fail-Environment "QT_ROOT_DIR is not set: set it to the Qt kit directory, the one holding bin\qmake.exe, or set it in a git-ignored local-env.ps1 beside this script."
}
$qmake = Join-Path $env:QT_ROOT_DIR 'bin\qmake.exe'
if (-not (Test-Path $qmake)) { Fail-Environment "No qmake.exe under `"$env:QT_ROOT_DIR\bin`"." }
$env:PATH = (Join-Path $env:QT_ROOT_DIR 'bin') + ';' + $env:PATH

if (-not $NoBuild)
{
	if (-not (Get-Command cl -ErrorAction SilentlyContinue))
	{
		$installerDirectory = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
		$vswhere = Join-Path $installerDirectory 'vswhere.exe'
		if (-not (Test-Path $vswhere)) { Fail-Environment 'vswhere.exe was not found. Install Visual Studio with the C++ toolset.' }

		$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
			Select-Object -First 1
		if (-not $visualStudio) { Fail-Environment 'No Visual Studio with the C++ toolset was found.' }

		# vcvars64.bat itself calls a bare vswhere.exe, which it expects on PATH
		$env:PATH += ';' + $installerDirectory
		# A child process's environment is not inherited back, so vcvars64.bat's is read out of cmd and imported
		$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
		cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
			if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
		}
	}

	Push-Location $testsDirectory
	try
	{
		# A kept .qmake.stash pins the toolchain probed when it was written, so every run probes the current one instead
		Remove-Item '.qmake.stash' -Force -ErrorAction SilentlyContinue
		& $qmake -tp vc image-processing-tests.pro
		if ($LASTEXITCODE -ne 0) { exit 1 }
		# msbuild, not nmake: it also rebuilds what the compiler command line changed for, such as the Qt include paths
		msbuild /t:Build /nologo /m /v:minimal "/p:Configuration=$Configuration" image-processing-tests.vcxproj
		if ($LASTEXITCODE -ne 0) { exit 1 }
	}
	finally { Pop-Location }
}

$binaries = Join-Path $testsDirectory "bin\$Configuration"
$executable = Join-Path $binaries 'image-processing-tests.exe'
if (-not (Test-Path $executable)) { Fail-Environment "Not built: `"$executable`"." }

if (-not $Benchmark)
{
	& $executable @CatchArguments
	if ($LASTEXITCODE -ne 0) { exit 1 }
	exit 0
}

# Keeps QImage::scaled, the control, single-threaded
$env:QT_NO_GUI_THREADPOOL = '1'
$results = Join-Path $binaries 'benchmark-results.xml'
# Catch2 rejects a repeated option, so a default is added only when the caller did not pass that option
[string[]]$defaults = @()
if ($CatchArguments -notcontains '--benchmark-samples') { $defaults += '--benchmark-samples', '10' }
if ($CatchArguments -notcontains '--benchmark-no-analysis') { $defaults += '--benchmark-no-analysis' }
& $executable '[!benchmark]' @defaults --reporter xml --out $results @CatchArguments
if ($LASTEXITCODE -ne 0) { exit 1 }
python (Join-Path $testsDirectory 'report_benchmark_ratios.py') $results
if ($LASTEXITCODE -ne 0) { exit 1 }
exit 0
