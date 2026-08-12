# Running a Hyperian application

Your application is the file ending in `.hyp`. It contains your left-aligned English MVC source.

From this extracted toolchain folder, run it on Windows with:

```text
bin\hyperian.exe run path\to\app.hyp
```

On Linux or macOS, run:

```text
./bin/hyperian run path/to/app.hyp
```

You can also place the `.hyp` filename directly after Hyperian:

```text
./bin/hyperian path/to/app.hyp
```

For detailed help, say:

```text
./bin/hyperian help run
```

Web, installable-web, and API applications print an address to open in a browser. Stop them with Ctrl+C. Console and service applications use the terminal. Desktop, mobile-preview, and game applications need the native backends reported by `hyperian doctor`.

A `.hyr` file is not an application and does not open by itself. It is an advanced native runtime pack used by `hyperian build ... for PLATFORM using ...` when building an application for another operating system or processor.
