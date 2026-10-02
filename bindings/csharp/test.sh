#!/bin/bash
# Build and run the C# binding's tests against the library in ../../bin.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
dotnet build "$here/Mobius.Tests" -c Release -v q -nologo
LD_LIBRARY_PATH="$here/../../bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    dotnet "$here/Mobius.Tests/bin/Release/net8.0/Mobius.Tests.dll"
