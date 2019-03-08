# Electron-shared

A (proof of concept?) launcher for Electron apps  
No need to include Electron with your apps, just use this? (or that's the theory)

1) Bundle your app in a `app.asar` package, or in a directory named `app` alongside this executable
2) Run it
3) Electron will be automatically downloaded if needed and your app will run

Electron-shared will look inside your `package.json` to check version requirements  
If a version of electron meeting these hasn't already be downloaded it will do so (with an update dialog while it does so)  
Runtimes get stored in `AppData\Local\Local\electron-shared`, `.cache/electron-shared` and `Library/Application Support/electron-shared`  
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
