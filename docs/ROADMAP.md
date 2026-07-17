# MeowOS Roadmap

## Phase 0 - Design

Goals:

- Define architecture
- Define package format
- Define database model
- Define repository structure


## Phase 1 - Local Package Manager

Goal:

Install local packages.

Features:

- Read package.toml
- Open tar.xz packages
- Extract files
- Track installed files
- SQLite database

Commands:


meow install package.tar.xz
meow remove package
meow list
meow info package



## Phase 2 - Dependency Resolver

Features:

- Dependency graph
- Dependency ordering
- Missing dependency detection
- Conflict detection


## Phase 3 - Repository System

Features:

- Remote repositories
- Package indexes
- Searching packages
- Synchronizing repositories


## Phase 4 - Security

Features:

- Package signatures
- Repository signatures
- Hash verification


## Phase 5 - Builder

Features:

- Read recipes
- Download sources
- Verify sources
- Build software
- Generate packages


## Phase 6 - Bootstrap

Features:

- Create root filesystem
- Build base packages
- Create usable system


## Phase 7 - Bootable System

Features:

- Linux kernel
- OpenRC
- Initramfs
- Bootloader
- Login


## Phase 8 - Desktop

Features:

- Wayland
- Mesa
- wlroots
- XWayland support
- Desktop environment/window manager


## Phase 9 - Installer

Features:

- Partitioning
- Filesystem creation
- Package installation
- Bootloader setup


## Phase 10 - Binary Infrastructure

Features:

- Build servers
- CI builds
- Package signing
- Repository mirrors