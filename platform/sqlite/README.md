# Embedded SQLite for Android

`sqlite3.c` and `sqlite3.h` are the official SQLite 3.53.4 amalgamation downloaded from:

https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip

The downloaded archive has SHA3-256 digest:

`628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`

SQLite's authors dedicate its deliverable source code to the public domain. Hyperian keeps these files unchanged and compiles them into generated Android applications so English `store data in sqlite file` declarations work without depending on Android's private SQLite C API.
