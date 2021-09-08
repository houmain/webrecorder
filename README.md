# webrecorder

A web server which forwards requests and records/replays responses.

    Usage: webrecorder [-options] [url|file]
      -u, --url <url>            set initial request URL.
      -f, --file <file>          set input/output file.
      -i, --input <file>         set input file.
      -o, --output <file>        set output file.
      -r, --refresh <mode>       refresh policy:
                                  never (default)
                                  when-expired
                                  when-expired-async
                                  always
      --refresh-timeout <sec.>   refresh timeout (default: 1).
      --request-timeout <sec.>   request timeout (default: 5).
      --no-append                do not keep not requested files.
      --no-download              do not download missing files.
      --allow-lossy-compression  allow lossy compression of big images.
      --block-hosts-file <file>  block hosts in file.
      --inject-js-file <file>    inject JavaScript in every HTML file.
      --patch-base-tag           patch base tag so URLs are relative to original host.
      --open-browser             open browser and navigate to requested URL.
      --proxy <host[:port]>      set a HTTP proxy.
      --exit-method <name>       defines a HTTP method used to exit the application.
      -h, --help                 print this help.
