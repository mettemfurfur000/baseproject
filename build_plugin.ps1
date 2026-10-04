$ErrorActionPreference = "Continue"

$root = $PSScriptRoot
$plugin = Join-Path $root "java\plugin"
$nativeLibrary = Join-Path $root "build\griefprot_ffi.dll"
$stagedLibrary = Join-Path $plugin "src\main\resources\native\griefprot_windows_x64.dll"
$maven = "C:\msys64\home\tem\maven\apache-maven-3.9.11\bin\mvn.cmd"
$buildLog = Join-Path $env:TEMP "griefprot-maven-build.log"

$env:JAVA_HOME = "C:\Program Files\Java\jdk-25.0.2"

if (-not (Get-Command make -ErrorAction SilentlyContinue)) {
    throw "GNU make is required to build the native ABI library."
}
if (-not (Test-Path $maven)) {
    throw "Maven was not found at $maven."
}

Push-Location $root
try {
    & make abi
    if ($LASTEXITCODE -ne 0) {
        throw "Native ABI build failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path $nativeLibrary)) {
        throw "Native ABI build did not produce $nativeLibrary."
    }

    Copy-Item $nativeLibrary $stagedLibrary -Force -ErrorAction Stop

    Push-Location $plugin
    try {
        & $maven clean package 2>&1 | Out-File $buildLog -Encoding utf8
        $mavenExitCode = $LASTEXITCODE

        Select-String -Path $buildLog -Pattern "\[INFO\] Building|Tests run:|BUILD (SUCCESS|FAILURE)" |
            Select-Object -First 8 |
            ForEach-Object { $_.Line.Trim() }

        if ($mavenExitCode -ne 0) {
            Get-Content $buildLog -Tail 40
            throw "Maven build failed with exit code $mavenExitCode. Full log: $buildLog"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    Pop-Location
}
