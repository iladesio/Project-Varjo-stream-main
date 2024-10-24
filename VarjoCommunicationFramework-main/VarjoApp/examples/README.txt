
Building and running examples:

  When building examples, choose "Release" configuration for testing and
  performance, "Debug" for development and debugging purposes.

a) Using CMake from command line

  1. Create a build directory.
  2. Go into your build directory.
  3. Generate VS project files:
     > cmake -G "Visual Studio 16 2019" -A x64 -T host=x64 ..
  4. Build project:
     For release build (preferred):
       > cmake --build . --config Release
     For debug build:
       > cmake --build . --config Debug
  5. Examples are built in "bin/" under your build directory.

  Alternatively you can open the generated VarjoExamples.sln solution file
  in Visual Studio IDE.

b) Using integrated CMake from Visual Studio

  1. File > Open > CMake.
  2. Select CMakeLists.txt from varjo-sdk/examples folder and open it.
  3. CMake > Build All.
  4. Select example application you want to run and start it.
