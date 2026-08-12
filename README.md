# Hyperian

Hyperian is an English-like, general-purpose language where every application is organized using Model–View–Controller. It is not Python, JavaScript, or a framework layered on either one. Its native C toolchain contains:

- a lexer and parser for `.hyp` source files;
- an MVC-aware compiler with friendly errors;
- a custom binary bytecode format (`.hyc`);
- a bytecode virtual machine;
- separate native runtimes for different application targets.

The goal is that a beginner can read the source aloud and understand it. Web, installable offline web application, console, API, one-shot service, native GTK desktop, phone-sized mobile preview, and native SDL2 game programs are executable today. Mobile programs can also be compiled into versioned Android or iOS deployment packages, driven through the native mobile runtime library, or built as signed APK, AAB, and IPA artifacts when the platform SDK and signing identity are installed. Android exports include a native Android Studio project, and iOS exports include a native SwiftUI Xcode project.

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
build/hyperian debug examples/blocks_game.hyp --event "player presses right"
build/hyperian debug examples/mobile_tasks.hyp --event "input title changes"
build/hyperian debug examples/mobile_tasks.hyp --event "application resumes"
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

The compiler recognizes all eight targets and records the target in bytecode. Its native HTTP runtime runs `web`, installable web applications, and `api`; the terminal runs `console` and `service`; GTK widgets run `desktop` and the phone-sized `mobile` preview; and SDL2 runs `game`. GTK3 and SDL2 are optional native build dependencies. The mobile preview validates interface behavior on a development computer. Android and iOS bytecode deployment packages, the portable native runtime bridge, native Android project generation, native SwiftUI Xcode project generation, and signed APK/AAB/IPA build orchestration are implemented. Producing a real signed artifact requires the corresponding platform SDK and the developer's signing identity.

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

Build a signed phone application directly with another English-shaped command:

```sh
hyperian build MyPhoneApp/app.hyp for android as MyPhoneApp.apk
hyperian build MyPhoneApp/app.hyp for android as MyPhoneApp.aab
hyperian build MyPhoneApp/app.hyp for ios as MyPhoneApp.ipa
```

Hyperian first compiles and exports the application into a private temporary folder, invokes the native platform builder without a shell, checks that the expected signed artifact was created, copies it to the requested filename without overwriting an existing file, and removes the temporary project. Signing passwords stay in environment variables and are never passed as command arguments.

### Native Android project

The generated Android project uses Android Gradle Plugin 9.3, API level 37, CMake, JNI, and the Android NDK. It contains the Hyperian C runtime sources and compiled application bytecode, so it does not depend on Python, JavaScript, a web server, or the desktop compiler at runtime.

Its Java Activity turns the bridge’s current view into native Android headings, text, inputs, text areas, checkboxes, buttons, links, and images. It synchronizes native input values into Hyperian state before button actions, change events, keyboard submission events, lifecycle events, taps, holds, and swipes; the native VM then executes the English controller behavior, persistent model work, expressions, and view navigation. Repeating Hyperian timers, pause/resume, focus/unfocus, taps, holds, and four-direction fling gestures are dispatched on Android’s main event loop.

English `get "https://..." from web` statements use Android's native HTTPS connection support with the operating system's certificate and hostname checks. `store data in sqlite file` uses the official SQLite engine compiled into the generated native application. Both HDB and SQLite data live in the application's private `hyperian-data.db` file, and the Android manifest includes internet permission.

Open the exported `android/` folder in Android Studio and run its `app` configuration, or let Hyperian create a signed release artifact. A local Android SDK, NDK, Gradle, and JDK 17 or newer are required. Configure signing without putting secrets in source code:

```sh
export HYPERIAN_APPLICATION_ID=com.example.mytasks
export HYPERIAN_ANDROID_KEYSTORE=/secure/release.jks
export HYPERIAN_ANDROID_KEY_ALIAS=release
export HYPERIAN_ANDROID_STORE_PASSWORD='your store password'
export HYPERIAN_ANDROID_KEY_PASSWORD='your key password'
hyperian build MyPhoneApp/app.hyp for android as MyPhoneApp.aab
```

Set `HYPERIAN_GRADLE` to an executable path if `gradle` is not on `PATH`. `.apk` runs Gradle's release APK task; `.aab` runs its release bundle task. Hyperian validates the application identifier, keystore, alias, and passwords before starting Gradle.

### Native iOS project

The generated iOS project embeds the same C runtime and `.hyc` bytecode. An Objective-C class owns the native Hyperian session and exposes it to Swift through a bridging header. SwiftUI supplies the app entry point and native headings, text, values, fields, text editors, toggles, buttons, links, images, navigation, repeating timers, live input changes, keyboard submission, scene lifecycle events, and simultaneous tap, hold, and swipe recognition.

On iPhone and iPad, English web requests use URLSession and the operating system's normal TLS checks. The generated Xcode project links SQLite, so the same English storage declaration persists models in the application's private `hyperian-data.db` file.

Open `ios/HyperianIOS.xcodeproj` in Xcode, choose a development team and unique bundle identifier, and run the shared `HyperianIOS` scheme, or let Hyperian archive and export a signed IPA:

```sh
export HYPERIAN_APPLICATION_ID=com.example.mytasks
export HYPERIAN_IOS_TEAM=ABCDE12345
export HYPERIAN_IOS_DISTRIBUTION=app-store-connect
hyperian build MyPhoneApp/app.hyp for ios as MyPhoneApp.ipa
```

Distribution may be `app-store-connect`, `ad-hoc`, `development`, or `enterprise`. Set `HYPERIAN_XCODEBUILD` when `xcodebuild` is not on `PATH`. The project targets iOS 16 or newer and stores HDB data in the app's private Documents folder. Xcode still requires a valid Apple Developer identity and provisioning access. Because this development environment is Linux and has no Xcode or Apple SDK, project structure, shared-scheme wiring, archive/export arguments, export options, artifact handoff, native bridge behavior, and bytecode identity are tested with a deterministic SDK stand-in, but a real Apple-signed IPA is not claimed as locally built.

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

Names can be readable quoted phrases instead of programming-style underscore names. This works for models, fields, controllers, values, action inputs and results, form controls, collections, native events, and game state:

```hyperian
model "Blog Post"
    field "display title" is text required
end

set "visit count" to 1
set "visit count" to value called "visit count" plus 1

if value called "visit count" is greater than 1
    set "page message" to "Welcome back"
end

show "page message"
```

Use `value called "visit count"` when a phrase-named value appears inside a calculation or condition. A quoted text value remains literal even when it contains words such as `plus` or `is`. Existing single-word and underscore names remain compatible. See [examples/english_names.hyp](examples/english_names.hyp), [examples/english_names_web.hyp](examples/english_names_web.hyp), [examples/mobile_connected.hyp](examples/mobile_connected.hyp), and [examples/physics_game.hyp](examples/physics_game.hyp).

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

Controllers can respond to native controls using complete English sentences:

```hyperian
when input title changes
    set live_preview to "Typing:" joined with title
end

when input title is submitted
    run action "save note"
end
```

Change events work with single-line inputs, text areas, and checkboxes. Submission events run when someone submits a single-line field from the keyboard. The GTK desktop and mobile-preview backends, native mobile C bridge, generated Android project, and generated iOS project all dispatch the same compiled controller events. The compiler rejects missing controls, text-area submission handlers, and use outside desktop or mobile applications.

Application and focus lifecycle behavior is English too:

```hyperian
when application pauses
    set connection_status to paused
end

when application resumes
    run action "refresh data"
end

when window gains focus
    set window_status to focused
end

when window loses focus
    set window_status to unfocused
end
```

Android lifecycle methods and iOS scene phases send pause/resume and focus/unfocus events through the native bridge. GTK windows send the same focus events for desktop applications and mobile previews. Pause and resume are mobile-only; the compiler prevents accidental use on targets that cannot deliver them. The source formatter and debugger understand the complete English phrases.

Touch gestures are controller events rather than platform callbacks:

```hyperian
when someone swipes left
    open view "next page"
end

when someone swipes down
    run action "refresh data"
end

when someone taps
    set gesture_status to "Tapped"
end

when someone presses and holds
    open view "details"
end
```

The Android project recognizes native taps, holds, and fling distance and velocity. iOS recognizes exclusive taps and holds plus simultaneous drags without replacing scrolling. The GTK mobile preview recognizes short presses, half-second holds, and mouse or touch drags. Every platform dispatches the same native VM events. These gesture sentences are mobile-only, invalid swipe directions produce a friendly compiler error, and the debugger accepts phrases such as `--event "someone taps"`, `--event "someone presses and holds"`, and `--event "someone swipes right"`.

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

Collections can be filtered by a literal or controller value, ordered numerically or alphabetically, and reversed—all in the same English instruction:

```hyperian
collect every Task title where status is wanted_status as matching_titles
collect every Task title ordered by priority as priority_titles
collect every Task title ordered by priority descending as reverse_priority_titles
collect every Task title where status is open ordered by priority as open_titles
collect every Task title where status is open ordered by priority descending as newest_open_titles
```

Collected, filtered, and ordered fields must exist and cannot be secret. The compiler checks every field before producing bytecode. The native VM runs the same query behavior over HDB and SQLite data; numeric order fields sort as numbers and other fields sort as text. Native repeated values are rendered as real GTK labels and are rebuilt when an action opens or refreshes the view. See [examples/native_queries.hyp](examples/native_queries.hyp).

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

model Score
    field points is number default 0
end

controller Game
    action initialize
        set "player left" to 100
        set "player top" to 100
        set "horizontal speed" to 200
        set "vertical speed" to 0
        set "ball horizontal center" to 500
        set "ball vertical center" to 320
        set "coin horizontal center" to 540
        set "coin vertical center" to 320
        set glow to 0
        set "animation frame" to 1
    end

    when application starts
        run action initialize
        show view "playfield"
    end

    when game updates
        move value glow toward 1 at 2 per second
        advance animation "animation frame" from 1 through 4 every 100 milliseconds
        apply gravity 300 to "vertical speed"
        move position "player left" "player top" using velocity "horizontal speed" "vertical speed"
        keep position "player left" "player top" inside 960 by 540 sized 64 by 64
        check whether rectangle at "player left" "player top" sized 64 by 64 touches rectangle at 400 260 sized 120 by 120 as "player hit"
        check whether circle centered at "ball horizontal center" "ball vertical center" with radius 24 touches circle centered at "coin horizontal center" "coin vertical center" with radius 12 as "coin hit"
        check whether rectangle at 400 260 sized 120 by 120 touches circle centered at "ball horizontal center" "ball vertical center" with radius 24 as "wall hit"
        check whether line from 0 0 to 960 540 touches line from 0 540 to 960 0 as "lines cross"
        check whether line from 0 0 to 960 540 touches circle centered at "ball horizontal center" "ball vertical center" with radius 24 as "laser hit"
        check whether rectangle at 400 260 sized 120 by 120 touches line from 0 270 to 960 270 as "wall crosses line"
    end
end

view "playfield"
    fill background with color 18 24 38
    draw rectangle at "player left" "player top" sized 64 by 64 with color 70 170 255
    draw circle centered at "ball horizontal center" "ball vertical center" with radius 24 and color 255 220 70
    draw line from 0 0 to 960 540 with color 120 230 160
    draw image "assets/player.bmp" at "player left" "player top" sized 64 by 64
end
```

The SDL2 backend dispatches arrow, space, and enter keys through `when player presses ...`, runs `when game updates` every frame, and exposes elapsed time as `seconds_since_last_frame`. English physics instructions apply frame-rate-independent velocity and gravity, clamp an object inside its play area, and test rectangle-to-rectangle, circle-to-circle, circle-to-rectangle, line-to-line, line-to-circle, and line-to-rectangle collisions. Either shape may be written first in a mixed collision phrase. Rectangle positions name their upper-left corner; circle positions name their center; lines name their two endpoints. Touching endpoints, overlapping collinear lines, and tangent circles count as contact. Negative sizes or radii stop with a readable error.

Animation needs no manual frame-time arithmetic. `move value` approaches its target without overshooting, whether the target is above or below the current value. `advance animation` changes a whole-number frame at the requested millisecond, second, or minute interval, keeps leftover time, catches up after a slow frame, and wraps from the final frame back to the first. The native VM performs both operations deterministically from elapsed frame time. The compiler restricts them to game applications, and invalid speeds, time, frames, or intervals stop with friendly errors.

Views can draw state-positioned BMP images, and actions can say `play sound "assets/jump.wav"` for WAV audio. See [examples/blocks_game.hyp](examples/blocks_game.hyp), [examples/game_media.hyp](examples/game_media.hyp), and [examples/physics_game.hyp](examples/physics_game.hyp).

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

The same English works in a native phone application, including SQLite models and HTTPS together:

```hyperian
application "Connected Tasks" is mobile
store data in sqlite file "connected-tasks.db"

controller Tasks
    action "refresh status"
        try
            get "https://example.com/status" from web as response and status as code
            set message to "Internet status:" joined with code joined with response
        when it fails as problem
            set message to problem
        end
    end
end
```

See [examples/mobile_connected.hyp](examples/mobile_connected.hyp) for the complete MVC application.

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

Version 0.39 is a small but real MVC platform: custom bytecode, a native VM, standalone executable and asset-bundle creation, readable quoted phrase names across MVC and every runtime, literal-safe English expressions, versioned Android/iOS mobile deployment packages, automated signed APK/AAB/IPA orchestration, a linkable native mobile runtime bridge, self-contained native Android Studio and SwiftUI Xcode project generation, native phone HTTPS and SQLite integration, native backend diagnostics, eight compiler targets including installable offline web applications, parallel-safe compiler tooling, an English source-line debugger, persistent model CRUD plus filtered and ordered collection queries inside native controller actions, repeated native collection views, recurring service and native-interface timers, interactive multi-view GTK desktop and phone-sized mobile preview interfaces with close, live-input-change, keyboard-submission, focus, pause, resume, tap, press-and-hold, and four-direction swipe events, matching generated Android/iOS lifecycle and gesture events, SDL2 keyboard/frame events, frame-rate-independent movement, gravity, smooth value transitions, timed animation frames, boundaries, rectangle, circle, and line drawing plus collision detection between every supported shape pair, BMP sprites, WAV sound, and state-driven rendering, native lists and maps, selectable HDB or transactional SQLite storage, recoverable runtime errors, an HTTP/HTTPS client, local packages, reusable actions, native file access, foldered project generation, versioned migrations, persistent CRUD models, safe HTML and typed JSON, authentication and authorization, middleware, formatting, and English tests.

The next major layers are polygon collision shapes, more media formats, and broader cross-compilation. Those are not claimed as complete yet.
