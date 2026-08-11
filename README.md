# Hyperian

Hyperian is an English-like, general-purpose language where every application is organized using Model–View–Controller. It is not Python, JavaScript, or a framework layered on either one. Its native C toolchain contains:

- a lexer and parser for `.hyp` source files;
- an MVC-aware compiler with friendly errors;
- a custom binary bytecode format (`.hyc`);
- a bytecode virtual machine;
- separate native runtimes for different application targets.

The goal is that a beginner can read the source aloud and understand it. Web, console, API, and one-shot service programs are executable today. Desktop and game backends fit behind the same MVC language design.

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
application "Greeter" is console
application "Drawing" is desktop
application "Accounts" is api
application "Worker" is service
application "Adventure" is game
```

The compiler recognizes all six targets and records the target in bytecode. `web`, `console`, `api`, and `service` have working native backends. Desktop and game programs currently give a clear “backend is not implemented yet” message when run; they are reserved backend boundaries, not fake aliases for another target.

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
```

Applications can replace the built-in missing-page response:

```hyperian
when error 404 show view "not-found"
```

## Programs split across files

Large applications can keep each MVC concern in its own file:

```hyperian
application "Modular App" is web

include "models.hyp"
include "controllers.hyp"
include "views.hyp"
```

Included paths are resolved relative to the file containing the `include`. Includes may be nested up to 32 levels. See [examples/modular/app.hyp](examples/modular/app.hyp).

## Static files and richer views

An application can expose a public directory without another web server:

```hyperian
serve files from "public" at "/assets"
```

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

Version 0.8 is a small but real MVC platform: custom bytecode, a native VM, target-aware compilation, working web/console/API/service runtimes, persistent models, complete CRUD, reusable layouts/components, static assets and richer controls, safe HTML and typed JSON, authentication and authorization, English controller logic, multi-file programs, middleware, custom error views, source formatting, and tests written in English.

The next major layers are explicit migrations, general middleware and error handlers, assets and richer controls, packages/modules, an integrated test language and debugger, then native desktop and game runtimes. Those are not claimed as complete yet.
