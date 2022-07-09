webrecorder
===========
A web server which forwards requests and records/replays responses.

It serves as the backend for the [Bookmark Hamster](https://github.com/houmain/hamster) but is intended to be a universal tool for similar use cases.

Usage example
-------------
```
webrecorder --open-browser --inject-js-file test/redirect_all.js --patch-base-tag "github.com/houmain"
```
By executing this command, a local webserver on some unassigned port is started. Which port and the progress in general is reported on the console output and could easily be processed by a frontend.

The `--open-browser` flag makes it automatically open the server's address in the system's default webbrowser.

The webserver forwards all requests to their original domain, serves the responses and records everything to a plain ZIP file. Unless explicitly specified on the commandline, the filename is deduced from the URL. For e.g. `github.com/houmain` it would be `github.com╱houmain` (notice the special unicode character for the slash).

By passing `--inject-js-file test/redirect_all.js` a JavaScript file is injected in each HTML response, which redirects all requests to other domains to the local server, so they are recorded too.
The `--patch-base-tag` flag is set, so relative URLs are automatically expanded to absolute URLs and therfore recorded in the correct domain's subdirectory.

The recording can be stopped by pressing `CTRL-C`.

When the same command is called again, the requests are served from the ZIP file and by default also kept up to date (just like the browser cache).

Command line arguments
----------------------

    Usage: webrecorder [-options] [url|file]
      -u, --url <url>            set initial request URL.
      -f, --file <file>          set input/output file.
      -i, --input <file>         set input file.
      -o, --output <file>        set output file.
      -d, --download <policy>    download policy:
                                    standard (default)
                                    always
                                    never
      -s, --serve <policy>       serve policy:
                                    latest (default)
                                    last
                                    first
      -a, --archive <policy>     archive policy:
                                    latest (default)
                                    first
                                    latest-and-first
                                    requested
      --refresh-timeout <secs>   refresh timeout (default: 1).
      --request-timeout <secs>   request timeout (default: 5).
      --localhost <hostname>     set hostname of local server (default: 127.0.0.1).
      --port <port>              set port of local server.
      --allow-lossy-compression  allow lossy compression of big images.
      --block-hosts-file <file>  block hosts in file.
      --inject-js-file <file>    inject JavaScript in every HTML file.
      --patch-base-tag           patch base tag so URLs are relative to original host.
      --open-browser             open browser and navigate to requested URL.
      --proxy <host[:port]>      set a HTTP proxy.
      -h, --help                 print this help.

Building
--------

A C++17 conforming compiler is required. A script for the
[CMake](https://cmake.org) build system is provided.

**Installing dependencies on Debian Linux and derivatives:**
```
sudo apt install build-essential git cmake libasio-dev libssl-dev
```

**Checking out the source:**
```
git clone https://github.com/houmain/webrecorder
```

**Building:**
```
cd webrecorder
cmake -B _build
cmake --build _build
```

License
-------

It is released under the GNU GPLv3. It comes with absolutely no warranty. Please see `LICENSE` for license details.
