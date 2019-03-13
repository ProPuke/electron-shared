# Electron-shared

[Usage](#usage)  
[How to Build](#building)

A (proof of concept?) launcher for Electron apps  
No need to include Electron with your apps, just use this? (or that's the theory)

1) Bundle your app in a `app.asar` package, or in a directory named `app` alongside this executable
2) Run it
3) Electron will be automatically downloaded if needed and your app will run

Electron-shared will look inside your `package.json` to check version requirements  
If a version of electron meeting these hasn't already be downloaded it will do so (with an update dialog while it does so)  
Runtimes get stored in `AppData\Local\electron-shared`, `.cache/electron-shared` and `Library/Application Support/electron-shared`  
Future apps compatible with the same versions will make use of the same downloaded runtimes

## Usage

```
Usage: ./electron-shared [options] [path]
       ./electron-shared [command]

  Download the latest supporting version of Electron for the specified PATH
  and then run the application using it.

  If omitted, path will default to "app"

  PATH must be a directory containing a project.json file or the path to an
  .asar archive (extension optional)

  Options:
    -d, --downloadOnly   Download any required Electron updates and then exit
    -n, --noDownload     Do NOT download if required Electron is not available
    -s, --silent         Do not display any user feedback while downloading

    Any other options will be passed directly to Electron (if it is executed)

  Commands:
    -h, --help           Display this help and exit
    -l, --list           Print the currently downloaded Electron versions
    -v, --version        Output version information and exit
```

## Building

Building Electron-shared has only currently been tested from within Linux  
It is cross-compiled for Windows from within Linux via MinGW  
The makefiles *should* also work from within macOS, although this has not been tested  
If anyone wants to contribute a native build process for Windows this would be greatly appreciated

First, clone the repo along with all submodules, by using the `--recursive` flag:
```
git clone --recursive https://github.com/ProPuke/electron-shared.git
```

Building is then done via 3 makefiles:
* `makefile.posix` - This will compile for your local (posix-compliant) OS. Libraries such as libcurl will be dynamically linked.
* `makefile.win32` - This will compile for 32bit win32. Libraries such as libcurl will be statically compiled into the executable.
* `makefile` - This will simply call both of the above, producing both a native executable *and* a 32bit win32 executable for distribution

### Building for Linux (and macOS)

To build exclusively for Linux (and hopefully also macOS), ensure the following libraries are installed: `libcurl` and `libgtk-3-dev` and execute the posix makefile as follows:

```
make -f makefile.posix
```

This will generate a native `electron-shared` executable

### Building for Windows (from within Linux)

To build for Windows from within Linux ensure the following library is installed: `mingw-w64` and execute the windows makefile as follows:

```
make -f makefile.win32
```

This will generate an `electron-shared.exe` 32bit executable

### Building for Linux/macOS *and* Windows at once

To build for both targets at once, ensure you have the neccasary libaries for both, above, installed and simple execute the main makefile:

```
make
```

This will generate both a native `electron-shared` executable, and an `electron-shared.exe` 32bit win32 executable.
