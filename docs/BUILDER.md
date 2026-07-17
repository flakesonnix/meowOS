# Meow Builder


The builder creates installable packages.


Input:


recipes/package/package.toml



Output:


package-version-release-arch.tar.xz



Build process:


1. Read recipe

2. Download source

3. Verify checksum

4. Extract source

5. Apply patches

6. Configure

7. Compile

8. Install into DESTDIR

9. Create package


Example:


source

↓

build

↓

files/

↓

package.tar.xz