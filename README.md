# Hyperian

Hyperian is an English-like, general-purpose language where every application is organized using Model–View–Controller. It is not Python, JavaScript, or a framework layered on either one. Its native C toolchain contains:

- a lexer and parser for `.hyp` source files;
- an MVC-aware compiler with friendly errors;
- a custom binary bytecode format (`.hyc`);
- a bytecode virtual machine;
- separate native runtimes for different application targets.

The goal is that a beginner can read the source aloud and understand it. Web, installable offline web application, console, API, one-shot service, native GTK desktop, phone-sized mobile preview, and native SDL2 game programs are executable today. Mobile programs can also be compiled into versioned Android or iOS deployment packages and driven through the native mobile runtime library. Android exports include a native Android Studio project, and iOS exports include a native SwiftUI Xcode project.

## Build the compiler

You only need a C compiler and CMake:

```sh
cmake -S . -B build
cmake --build build
```

The compiler and VM are now at `build/hyperian`.

## Try it

Compile source into real Hyperian bytecode:

```sh
build/hyperian compile examples/tasks.hyp -o build/tasks.hyc
```

Run either source or compiled bytecode:

```sh
build/hyperian run examples/tasks.hyp
# or: build/hyperian run build/tasks.hyc
```

Build one executable containing the native runtime and compiled MVC bytecode:

```sh
build/hyperian build examples/report_service.hyp -o MyReport
./MyReport
```

The executable no longer needs the `.hyp` or `.hyc` file. Web and API executables accept `--port 9000`. Public assets and images remain beside it so they can be updated independently. Native GTK, SDL2, and other dynamically linked system libraries must be installed on the destination platform.

Create a release folder containing the executable and its `assets/` and `public/` files:

```sh
build/hyperian bundle examples/bundle_console/app.hyp -o BundleReader
./BundleReader/run
```

A bundle carries an `HYBN1` manifest. Its executable automatically uses the bundle folder as its working directory, so file access, game media, and public web assets work even when the program is launched elsewhere. Bundles target the operating system and CPU on which the compiler was built; native system libraries are still external dependencies.

See which backends were compiled into the current toolchain:

```sh
build/hyperian doctor
```

Open <http://127.0.0.1:8080>. Stop it with Ctrl+C.

The same compiler runs native console software:

```sh
build/hyperian run examples/greeter.hyp
```

```text
Friendly Greeter
What is your name? Ada

== Welcome ==
It is good to meet you,
Ada
```

Check a program or inspect the compiler’s output with:

```sh
build/hyperian check examples/tasks.hyp
build/hyperian inspect build/tasks.hyc
```

Trace an application in plain English and see every state change:

```sh
build/hyperian debug examples/blocks_game.hyp
build/hyperian debug examples/blocks_game.hyp --event KEY:right
build/hyperian debug examples/english_logic.hyp --action greet --input Kenneth
```

The debugger follows the same bytecode as the real application. It shows source line numbers, nested action calls, and the final state, so behavior does not need to be recreated in a separate interpreter.
Because it executes the real VM, actions that write files, call services, or change persistent models perform those changes while being debugged. Use `HYPERIAN_DATA` to point debugging at a separate data file when needed.

For a complete persistent CRUD example:

```sh
build/hyperian run examples/notes_crud.hyp
```

## The MVC idea

The meaning stays consistent for every target:

- A **model** describes the application’s information.
- A **view** describes what a person sees or hears.
- A **controller** describes responses to routes, input, timers, clicks, and other events.
- A **backend** translates those ideas into pages, terminal output, native windows, API responses, background processes, or game frames.

## Application targets

The first instruction declares what you are making:

```hyperian
application "Tasks" is web
application "Pocket Tasks" is installable web application
application "Greeter" is console
application "Drawing" is desktop
application "Pocket Tasks" is mobile
application "Accounts" is api
application "Worker" is service
application "Adventure" is game
```

The compiler recognizes all eight targets and records the target in bytecode. Its native HTTP runtime runs `web`, installable web applications, and `api`; the terminal runs `console` and `service`; GTK widgets run `desktop` and the phone-sized `mobile` preview; and SDL2 runs `game`. GTK3 and SDL2 are optional native build dependencies. The mobile preview validates interface behavior on a development computer. Android and iOS bytecode deployment packages, the portable native runtime bridge, native Android project generation, and native SwiftUI Xcode project generation are implemented. Signed store-ready builds are not complete yet.

Create a complete foldered project for any target:

```sh
hyperian new MyWebsite --target web
hyperian new MyPocketApp --target pwa
hyperian new MyTool --target console
hyperian new MyDesktopApp --target desktop
hyperian new MyPhoneApp --target mobile
hyperian new MyGame --target game
```

The `pwa` project option generates an installable web application with a manifest, icon, root-scoped service worker, and offline cache. Hyperian automatically connects those browser files to the rendered MVC view. The service worker is generated browser plumbing; application authors keep writing English-like Hyperian in their models, views, controllers, and application file. See [examples/pwa_tasks](examples/pwa_tasks).

Export a `mobile` application using an English-shaped command:

```sh
hyperian export MyPhoneApp/app.hyp for android to AndroidPackage
hyperian export MyPhoneApp/app.hyp for ios to IosPackage
```

Each `HYMB1` package contains the compiled `application.hyc`, copied `assets/` and `public/` folders when present, the intended phone platform, the toolchain version, and a versioned runtime-interface number. Export rejects non-mobile applications and never needs Python. Android packages contain a self-contained `android/` project, and iOS packages contain a self-contained `ios/HyperianIOS.xcodeproj`.

### Native Android project

The generated Android project uses Android Gradle Plugin 9.3, API level 37, CMake, JNI, and the Android NDK. It contains the Hyperian C runtime sources and compiled application bytecode, so it does not depend on Python, JavaScript, a web server, or the desktop compiler at runtime.

Its Java Activity turns the bridge’s current view into native Android headings, text, inputs, text areas, checkboxes, buttons, links, and images. Before a button action it synchronizes input values into Hyperian state; the native VM then executes the English controller action, persistent HDB model work, expressions, and view navigation. Repeating Hyperian timers are dispatched on Android’s main event loop.

Open the exported `android/` folder in Android Studio and run its `app` configuration. A local Android SDK, NDK, and JDK 17 or newer are required. Store distribution still requires choosing a unique application ID and signing the release with your own key. HTTPS/SQLite integration in the Android runtime and automated APK/AAB signing remain future layers.

### Native iOS project

The generated iOS project embeds the same C runtime and `.hyc` bytecode. An Objective-C class owns the native Hyperian session and exposes it to Swift through a bridging header. SwiftUI supplies the app entry point and native headings, text, values, fields, text editors, toggles, buttons, links, images, navigation, and repeating timers.

Open `ios/HyperianIOS.xcodeproj` in Xcode, choose a development team and unique bundle identifier, and run the `HyperianIOS` scheme. The project targets iOS 16 or newer and stores HDB data in the app's private Documents folder. Store distribution still requires an Apple Developer identity, provisioning, archiving, and signing. Because this development environment is Linux and has no Xcode or Apple SDK, project structure, XML, bridge wiring, runtime behavior, installed export resources, and bytecode identity are tested here, but an iOS simulator/device build is not claimed as locally verified.

## Native mobile runtime bridge

The build also produces `libhyperian_mobile.a`, a portable C library for Android NDK, iOS, and other native interface adapters. Its opaque session API is declared in `src/hyperian.h`:

- `hyperian_mobile_open` loads and verifies mobile bytecode and opens its persistent MVC data.
- `hyperian_mobile_start` runs `when application starts`.
- `hyperian_mobile_set` synchronizes a native input into controller state.
- `hyperian_mobile_run_action` and `hyperian_mobile_send_event` execute controller behavior, CRUD, timers, and navigation.
- `hyperian_mobile_render_json` returns the current view as ordered heading, text, value, input, text-area, checkbox, button, link, and image controls. It expands English `for each` and `if` view blocks.
- `hyperian_mobile_close` releases the session.

This API means platform code renders controls but does not reimplement Hyperian semantics. Models, actions, persistence, view selection, and expressions continue running in Hyperian’s native VM. CMake installs the library under `lib/` and its public header under `include/hyperian/`.

## Web example

```hyperian
application "Tiny Tasks" is web
listen on 8080

model Task
    field title is text required
end

controller Tasks
    when someone visits "/"
        find all Task as tasks
        show view "tasks" with tasks
    end

    when someone submits "/tasks"
        create Task from form
        redirect to "/"
    end
end

view "tasks"
    heading "My tasks"
    form posts to "/tasks"
        input "New task" as title required
        button "Add"
    end
    for each task in tasks show
        show task.title
    end
end
```

## Console example

```hyperian
application "Greeter" is console

model Person
    field name is text required
end

controller Greeting
    when application starts
        ask "What is your name?" as name
        show view "hello" with name
    end
end

view "hello"
    heading "Welcome"
    say "Hello,"
    show name
end
```

## Language words

Models support automatic IDs, persistent records, and `text`, `number`, and `boolean` fields. Rules are written after the type in any order:

```hyperian
field name is text required
field priority is number default 1
field published is boolean default false
field username is text required unique minimum 3 maximum 30
field author is reference Author required
```

Records are saved atomically in `hyperian-data.hdb`. Set the `HYPERIAN_DATA` environment variable when each app should use a different location.

Web controllers support the complete CRUD cycle:

```hyperian
find all Note as notes
find Note by route id as note
create Note from form
update Note using route id from form
delete Note using route id
```

Parameterized routes use `{id}`:

```hyperian
when someone visits "/notes/{id}"
when someone submits "/notes/{id}"
when someone submits "/notes/{id}/delete"
```

Views can build record-specific URLs, and edit forms are automatically filled from the current record:

```hyperian
link "Edit" to "/notes/{note.id}"
form posts to "/notes/{note.id}"
```

Collections can be ordered or filtered through a relationship:

```hyperian
find all Post ordered by title as posts
find Post where author is route id as posts
```

Layouts and components keep views reusable:

```hyperian
layout "application"
    heading "My App"
    content
end

component "post-link"
    show post.title
end

view "posts"
    use layout "application"
    for each post in posts show
        use component "post-link"
    end
end
```

API applications return correctly typed native JSON:

```hyperian
application "Blog API" is api

controller Posts
    when someone visits "/posts"
        find all Post ordered by title as posts
        show json posts
    end
end
```

See [examples/blog_api.hyp](examples/blog_api.hyp) for models with relationships, ordered and filtered queries, and collection and record JSON responses.

## Authentication and protected controllers

Declare passwords or API credentials as `secret`. Hyperian derives a password hash using PBKDF2-HMAC-SHA-256 with a random salt and never returns secret fields in HTML values or JSON:

```hyperian
model User
    field email is text required unique
    field password is secret required minimum 8 maximum 200
    field role is text default member protected
end
```

`protected` fields ignore browser form values, which prevents people from assigning themselves roles or other server-controlled values.

Authentication remains English-like:

```hyperian
when someone submits "/login"
    sign in User using email and password
    redirect to "/dashboard"
end

when someone visits "/dashboard"
    require sign in or redirect to "/"
    find signed in User as user
    show view "dashboard" with user
end

when someone submits "/logout"
    sign out
    redirect to "/"
end
```

Field-based authorization can protect a controller after loading the signed-in record:

```hyperian
find signed in User as user
require user.role is "admin" or redirect to "/"
show view "admin" with user
```

Use `secret input "Password" as password required` in a view. Sessions use random 256-bit tokens in `HttpOnly`, `SameSite=Lax` cookies. See [examples/accounts.hyp](examples/accounts.hyp) for registration, sign-in, protected content, current-user lookup, and sign-out.

Sessions currently live in server memory and cookies are not marked `Secure` because the development server is HTTP-only. A production TLS deployment must add a secure reverse proxy and durable or distributed session storage.

## English controller logic

Reusable actions, variables, arithmetic, conditions, and loops use words instead of punctuation-heavy expressions:

```hyperian
controller Calculator
    action "calculate the total"
        set total to 2

        repeat 4 times
            set total to total plus 3
        end

        if total is greater than 10
            set message to "The total is large"
        otherwise
            set message to "The total is small"
        end
    end

    when someone visits "/"
        run action "calculate the total"
        show view "result"
    end
end
```

Expressions support `plus`, `minus`, `times`, `divided by`, and `joined with`. Conditions support `is`, `is not`, `is greater than`, `is less than`, and `contains`. Controller variables can be shown directly in views or returned as JSON.

Web controllers can read ordinary form values before running an action:

```hyperian
read price from form as price
read quantity from form as quantity
run action "calculate the total"
```

The same action engine runs in web, API, console, and service programs. See [examples/english_logic.hyp](examples/english_logic.hyp) and [examples/report_service.hyp](examples/report_service.hyp).

Hyperian also includes a source formatter:

```sh
build/hyperian format examples/english_logic.hyp
```

## Tests written in English

Tests live beside the controller actions they verify:

```hyperian
test "repeating addition"
    set total to 2
    repeat 4 times
        set total to total plus 3
    end
    expect total to be 14
end
```

Run every test without starting the application:

```sh
build/hyperian test examples/english_logic.hyp
```

## Middleware and error views

A controller action can prepare every route:

```hyperian
action "prepare every request"
    set application_name to "My App"
end

before every route run action "prepare every request"

before route "/admin" run action "require an administrator"
```

Actions can accept an input and return a result, making them readable functions:

```hyperian
action "greet person" using person
    return "Hello," joined with person
end

run action "greet person" using Ada as greeting
```

Native applications can read and atomically write text files:

```hyperian
write greeting to file "greeting.txt"
read file "greeting.txt" as saved_greeting
```

## Lists, recoverable errors, and web requests

Lists work in controllers, tests, console views, and web views:

```hyperian
make list colors
add red to colors
add "deep blue" to colors
count colors as color_count
take item 2 from colors as second_color
remove red from colors
```

Maps keep values under readable keys:

```hyperian
make map settings
put dark as theme in settings
put 20 as page_size in settings
take key theme from settings as selected_theme
remove key page_size from settings
count entries in settings as setting_count
```

Risky work can be recovered without stopping the application:

```hyperian
try
    read file "settings.txt" as settings
when it fails as problem
    set settings to "default settings"
end
```

The native HTTP client supports HTTP, HTTPS, redirects, timeouts, response bodies, and status codes:

```hyperian
try
    get "https://example.com/api" from web as response and status as status_code
when it fails as problem
    set response to problem
end
```

Applications can replace built-in responses for any HTTP error from 400 through 599. The error view can show the numeric `status` value:

```hyperian
when error 404 show view "not-found"
when error 500 show view "server-problem"
```

## Native desktop and games

Desktop views become real GTK controls:

```hyperian
application "Desktop Notes" is desktop

controller Notes
    action "save note"
        set status to "Saved:" joined with title
    end

    when application starts
        set status to ready
        show view "notes"
    end
end

view "notes"
    heading "Native desktop notes"
    input "Note title" as title
    textarea "Note details" as details
    checkbox "Important" as important
    button "Save note" runs action "save note"
    show status
end
```

GTK inputs synchronize into controller state before the action runs. Afterwards, `show` labels refresh from the updated state. Actions may use the same calculations, lists, maps, files, HTTP requests, and recoverable errors as every other target.

Desktop and mobile actions can navigate between native MVC views in English:

```hyperian
action "show settings"
    open view "settings"
end

when window closes
    write settings to file "settings.txt"
end
```

Opening a view rebuilds the native controls while keeping controller state. The close event can run ordinary action logic for cleanup or saving.

Native controller actions use the same persistent models, validation rules, secret hashing, HDB files, SQLite option, and migrations as web applications. CRUD remains readable:

```hyperian
action "save note"
    create a Note using the current values as note_id
    find the Note numbered note_id as saved_note
    set title to "A better title"
    update the Note numbered note_id using the current values
    count all Note records as note_count
end

action "remove note"
    delete the Note numbered note_id
end
```

“Current values” are controller values and synchronized view inputs whose names match model fields. Finding a record exposes values such as `saved_note_title`, `saved_note_id`, and `saved_note_found`, making them available to later actions and reactive views. See [examples/native_crud.hyp](examples/native_crud.hyp) and [examples/desktop_notes.hyp](examples/desktop_notes.hyp).

Controllers can collect one safe field from every record into a normal Hyperian list, and native or console views can repeat that list:

```hyperian
action "load note titles"
    collect every Note title as note_titles
end

view "notes"
    heading "Saved notes"
    for each note_title in note_titles show
        show note_title
    end
end
```

Collected fields must exist and cannot be secret. The compiler checks both rules. Native repeated values are rendered as real GTK labels and are rebuilt when an action opens or refreshes the view.

Services, desktop apps, and mobile previews can schedule recurring controller work:

```hyperian
action "check for work"
    set checks to checks plus 1
end

every 5 seconds
    run action "check for work"
end
```

Intervals may use milliseconds, seconds, or minutes and can be at most one day. Service applications remain alive while scheduled work exists and stop cleanly on Ctrl+C or termination. Native interface timers update reactive values and may open another view.

Game views are rendered in a native SDL2 window and event loop:

```hyperian
application "Blocks" is game

controller Game
    action "move right"
        set player_x to player_x plus 20
    end

    when player presses right
        run action "move right"
    end

    when game updates
        run action "update world"
    end
end

view "playfield"
    fill background with color 18 24 38
    draw rectangle at player_x player_y sized 180 by 80 with color 70 170 255
    draw image "assets/player.bmp" at player_x player_y sized 64 by 64
end
```

The SDL2 backend dispatches arrow, space, and enter keys through `when player presses ...`, runs `when game updates` every frame, and exposes elapsed time as `seconds_since_last_frame`. Views can draw state-positioned BMP images, and actions can say `play sound "assets/jump.wav"` for WAV audio. The `mobile` target uses the same controls and actions in a narrow touch-friendly preview window. See [examples/desktop_notes.hyp](examples/desktop_notes.hyp), [examples/mobile_tasks.hyp](examples/mobile_tasks.hyp), [examples/blocks_game.hyp](examples/blocks_game.hyp), and [examples/game_media.hyp](examples/game_media.hyp). Physics, more image/audio formats, animation helpers, and deployable phone packaging remain upcoming layers.

## Programs split across files

Large applications can keep each MVC concern in its own file:

```hyperian
application "Modular App" is web

include "models.hyp"
include "controllers.hyp"
include "views.hyp"
```

Included paths are resolved relative to the file containing the `include`. Includes may be nested up to 32 levels. See [examples/modular/app.hyp](examples/modular/app.hyp).

A project can use as many folders and files as it needs:

```text
my_app/
├── app.hyp
├── models/
│   └── task.hyp
├── controllers/
│   └── tasks.hyp
├── views/
│   ├── task-list.hyp
│   └── shared/
│       └── layouts.hyp
├── public/
    └── app.css
└── packages/
    └── text_tools/
        └── package.hyp
```

The main file stays simple:

```hyperian
application "Foldered Tasks" is web
serve files from "public" at "/assets"

include "models/task.hyp"
include "controllers/tasks.hyp"
include "views/task-list.hyp"
```

An included file can include another nearby file, such as `include "shared/layouts.hyp"`. The compiler combines the whole project into one self-contained `.hyc` program. Public folders are resolved beside the main source file. See [examples/foldered_app/app.hyp](examples/foldered_app/app.hyp).

Reusable local packages live under `packages/<name>/package.hyp`:

```hyperian
use package "text_tools"
```

Set `HYPERIAN_PACKAGES` to share a package collection between projects. Package files can declare models, controllers, actions, views, layouts, and components, and can include their own nearby source files. See [examples/core_features](examples/core_features).

## Static files and richer views

An application can expose a public directory without another web server:

```hyperian
serve files from "public" at "/assets"
```

## Data migrations

Hyperian stores a version inside its native data file. Describe model changes in English and Hyperian upgrades old data automatically before the application starts:

```hyperian
data version 2

when data changes from 1 to 2
    rename field title to name in model Task
end

model Task
    field name is text required
end
```

Apply migrations without starting the application:

```sh
hyperian migrate app.hyp
```

Every version must move forward one step. Hyperian refuses to open data created by a newer application version, writes migrations atomically, and automatically upgrades older `HDB1` files into versioned `HDB2` files.

HDB remains the dependency-free default. Applications can select SQLite while keeping the same models, validation, CRUD routes, relationships, APIs, and English migrations:

```hyperian
store data in sqlite file "application.db"
```

SQLite writes use transactions, schema metadata tracks the data version, and `HYPERIAN_DATA` can override the declared path for tests or deployment. See [examples/sqlite_tasks.hyp](examples/sqlite_tasks.hyp).

Static requests reject parent-directory traversal and are returned as binary-safe responses with appropriate CSS, JavaScript, SVG, PNG, JPEG, GIF, WebP, icon, HTML, JSON, and text content types.

Views connect those assets and provide richer form controls in plain English:

```hyperian
view "editor"
    style "/assets/app.css"
    script "/assets/app.js"
    image "/assets/logo.svg" described as "Application logo"

    form posts to "/articles"
        input "Title" as title required
        textarea "Article body" as body required
        checkbox "Publish now" as published
        button "Save article"
    end
end
```

Edit views populate text areas and checkbox state from the current model. HTML attributes and values remain escaped. The complete multi-file asset example is in [examples/modular](examples/modular).

Console controllers understand `when application starts` and `ask`. Views share `title`, `heading`, `text`, `say`, `show`, `for each`, and `if`; web views additionally support links and forms.

Quoted text may contain spaces. `#` starts a comment. Indentation is optional but encouraged. Every block closes with `end`.

## Project status

Version 0.27 is a small but real MVC platform: custom bytecode, a native VM, standalone executable and asset-bundle creation, versioned Android/iOS mobile deployment packages, a linkable native mobile runtime bridge, self-contained native Android Studio and SwiftUI Xcode project generation, native backend diagnostics, eight compiler targets including installable offline web applications, an English source-line debugger, persistent model CRUD and collection queries inside native controller actions, repeated native collection views, recurring service and native-interface timers, interactive multi-view GTK desktop and phone-sized mobile preview interfaces with close events, SDL2 keyboard/frame events, frame timing, BMP sprites, WAV sound, and state-driven rendering, native lists and maps, selectable HDB or transactional SQLite storage, recoverable runtime errors, an HTTP/HTTPS client, local packages, reusable actions, native file access, foldered project generation, versioned migrations, persistent CRUD models, safe HTML and typed JSON, authentication and authorization, middleware, formatting, and English tests.

The next major layers are automated APK/AAB/IPA builds and signing, HTTPS/SQLite phone integration, richer desktop and mobile events, game animation and physics helpers, more media formats, and broader cross-compilation. Those are not claimed as complete yet.
