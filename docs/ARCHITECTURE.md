# MeowOS Architecture


## Overview


package recipe

    |

    v

builder

    |

    v

package.tar.xz

    |

    v

repository

    |

    v

meow


## meow

The user-facing package manager.

Handles:

- Installation
- Removal
- Updates
- Verification
- Dependencies


## builder

Creates packages.

Input:


recipes/bash/package.toml


Output:


bash-5.3-1-x86_64.tar.xz



## repository

Stores:

- Packages
- Metadata
- Signatures
- Index database


## bootstrap

Creates the first MeowOS installation.
PACKAGE_FORMAT.md
# Meow Package Format


Packages use:


.tar.xz



Example:


bash-5.3-1-x86_64.tar.xz



Contents:


package.tar.xz

├── package.toml <br>
└── files/ <br>
├── usr/ <br>
├── etc/ <br>
└── var/ <br>



## Example package.toml

```toml
name = "bash"

source = "https://example.org/bash-${version}.tar.xz"


[[versions]]

version = "5.3"

sha256 = "..."
Package metadata

Possible fields:

name
version
release
architecture
dependencies
conflicts
provides
source
checksum

---

# `DATABASE.md`

```md
# Meow Database


Meow uses SQLite for installed package tracking.


Database location:


/var/lib/meow/meow.db



## packages

Stores installed packages.

Possible fields:


id
name
version
release
architecture
install_time



## files

Stores installed files.

Possible fields:


id
package_id
path
checksum
permissions



## dependencies

Stores relationships.

Possible fields:


package_id
dependency



## repositories

Stores configured repositories.

Possible fields:


name
url
priority