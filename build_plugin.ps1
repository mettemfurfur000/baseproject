cd C:\msys64\home\tem\griefprot\java\plugin; Copy-Item ..\..\build\griefprot_ffi.dll src\main\resources\native\griefprot_windows_x64.dll -Force; $env:JAVA_HOME="C:\Program Files\Java\jdk-25.0.2"; $m="C:\msys64\home\tem\maven\apache-maven-3.9.11\bin\mvn.cmd"; & $m clean package 2>&1 | Out-File C:\msys64\tmp\opencode\mvn16.log -Encoding utf8; Select-String -Path C:\msys64\tmp\opencode\mvn16.log -Pattern "Tests run:|BUILD|^\[ERROR\]" | Select-Object -First 8 | ForEach-Object { $_.Line.Trim() }


cd ..\..\
