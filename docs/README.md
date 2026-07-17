# MeowOS

MeowOS is a custom Linux distribution project focused on learning Linux internals, package management, build systems and distribution engineering.

## Goals

- Rolling release
- glibc based
- OpenRC init system
- Wayland focused desktop
- x86_64 first
- FHS filesystem layout
- Binary packages by default
- Optional source builds
- Classic Linux system
- GPLv3 licensed

## Main Components

### meow

The package manager.

Responsibilities:

- Install packages
- Remove packages
- Update packages
- Dependency resolution
- Package verification
- Repository handling

### builder

The package build system.

Responsibilities:

- Read package recipes
- Download sources
- Build software
- Create packages

### bootstrap

Creates the initial MeowOS system.

Responsibilities:

- Build base system
- Create root filesystem
- Prepare bootable installation

### recipes

Package definitions similar to nixpkgs.

Example:


recipes/ <br>
└── bash/ <br>
└── package.toml


## Philosophy

Keep the system simple, understandable and maintainable.

Do not add complexity unless it solves a real problem.