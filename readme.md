## Introduction

winnp is a plugin for Winamp (tested with 5.9.2) which logs each song played. Each song is appended to nowplaying.txt in the current user's Documents directory.

## Compiling

To clone and run this application, you'll need [Git](https://git-scm.com) and [.NET](https://dotnet.microsoft.com/) installed on your computer. From your command line:

```
# Clone this repository
$ git clone https://github.com/btigi/alarm

# Go into the repository
$ cd src

# Build  the app
$ msbuild winnp.sln /p:Configuration=Release
```

## Usage

Place the plugin file (gen_winnp.dll) in the Winamp plugin directory (default C:\Program Files (x86)\Winamp\Plugins). Each played song is automatically logged to nowplaying.txt in the current user's Documents directory.


## Licencing

winnp is licenced under the MIT license. Full license details are available in license.md