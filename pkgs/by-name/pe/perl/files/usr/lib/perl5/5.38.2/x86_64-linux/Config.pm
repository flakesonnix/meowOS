# This file was created by configpm when Perl was built. Any changes
# made to this file will be lost the next time perl is built.

# for a description of the variables, please have a look at the
# Glossary file, as written in the Porting folder, or use the url:
# https://github.com/Perl/perl5/blob/blead/Porting/Glossary

package Config;
use strict;
use warnings;
our ( %Config, $VERSION );

$VERSION = "5.038002";

# Skip @Config::EXPORT because it only contains %Config, which we special
# case below as it's not a function. @Config::EXPORT won't change in the
# lifetime of Perl 5.
my %Export_Cache = (myconfig => 1, config_sh => 1, config_vars => 1,
                    config_re => 1, compile_date => 1, local_patches => 1,
                    bincompat_options => 1, non_bincompat_options => 1,
                    header_files => 1);

@Config::EXPORT = qw(%Config);
@Config::EXPORT_OK = keys %Export_Cache;

# Need to stub all the functions to make code such as print Config::config_sh
# keep working

sub bincompat_options;
sub compile_date;
sub config_re;
sub config_sh;
sub config_vars;
sub header_files;
sub local_patches;
sub myconfig;
sub non_bincompat_options;

# Define our own import method to avoid pulling in the full Exporter:
sub import {
    shift;
    @_ = @Config::EXPORT unless @_;

    my @funcs = grep $_ ne '%Config', @_;
    my $export_Config = @funcs < @_ ? 1 : 0;

    no strict 'refs';
    my $callpkg = caller(0);
    foreach my $func (@funcs) {
        die qq{"$func" is not exported by the Config module\n}
            unless $Export_Cache{$func};
        *{$callpkg.'::'.$func} = \&{$func};
    }

    *{"$callpkg\::Config"} = \%Config if $export_Config;
    return;
}

die "$0: Perl lib version (5.38.2) doesn't match executable '$^X' version ($])"
    unless $^V;

$^V eq 5.38.2
    or die sprintf "%s: Perl lib version (5.38.2) doesn't match executable '$^X' version (%vd)", $0, $^V;


sub FETCH {
    my($self, $key) = @_;

    # check for cached value (which may be undef so we use exists not defined)
    return exists $self->{$key} ? $self->{$key} : $self->fetch_string($key);
}

sub TIEHASH {
    bless $_[1], $_[0];
}

sub DESTROY { }

sub AUTOLOAD {
    require 'Config_heavy.pl';
    goto \&launcher unless $Config::AUTOLOAD =~ /launcher$/;
    die "&Config::AUTOLOAD failed on $Config::AUTOLOAD";
}

# tie returns the object, so the value returned to require will be true.
tie %Config, 'Config', {
    archlibexp => '/usr/lib/perl5/5.38.2/x86_64-linux',
    archname => 'x86_64-linux',
    cc => 'cc',
    d_readlink => 'define',
    d_symlink => 'define',
    dlext => 'so',
    dlsrc => 'dl_dlopen.xs',
    dont_use_nlink => undef,
    exe_ext => '',
    inc_version_list => ' ',
    intsize => '4',
    ldlibpthname => 'LD_LIBRARY_PATH',
    libpth => '/nix/store/y3baf247w0620hii9sbadc7cqkzf2mcp-flex-2.6.4/lib /nix/store/gfkx12n2ph1j6wpq8dp8h6mdfgw0kxn2-sqlite-3.53.1-dev/lib /nix/store/wrfjbh7z8pqvl3x7r5h7l4lpg5w9sc2i-libarchive-3.8.8-dev/lib /nix/store/4221pk3r0s44lazwx1hrll1242ivdh6p-attr-2.5.2-dev/lib /nix/store/smy0i3g9wyhc5r75vlw4ggyngiz49d86-acl-2.3.2-dev/lib /nix/store/a22iqmpl93pfcl6q8gj8958j8mnvybir-tomlplusplus-3.4.0/lib /nix/store/jvnhc1z9c04n4a7b2z2hzbajsa5i6ygd-openssl-3.6.3-dev/lib /nix/store/sspqmllb6bwpysc3mvid7lds8j8hy35k-curl-8.21.0-dev/lib /nix/store/29p2wg0b6b7kz5qa49anlczi3b4sivn7-brotli-1.2.0-dev/lib /nix/store/hkyzkba1hay4m4irb6bssi0zb635shzy-krb5-1.22.2-dev/lib /nix/store/614rcv0ddpsrk1prfln6wnsfdjba182h-nghttp2-1.69.0-dev/lib /nix/store/9asdbcw5av7p0am8n0v58ch2ldzgzmrk-nghttp3-1.16.0-dev/lib /nix/store/dbbfnxh0308rbli2jblm19l6pcdk25sd-ngtcp2-1.23.0-dev/lib /nix/store/3f857xi9fgdcby52kklyhcc8g9cir38s-libidn2-2.3.8-dev/lib /nix/store/w2vbpjxqc9kgb4pf1pwwwlsb08jw7wqm-libpsl-0.21.5-dev/lib /nix/store/3z6fbqcfyq6k6qh3b0ssx9bkb8rxymc8-libssh2-1.11.1-dev/lib /nix/store/0n024dhzxyg45w8wzfshqx36gjggmqd2-zstd-1.5.7-dev/lib /nix/store/jxyrvv4gbpnp3ap5iy7wxwl1sg4x2x88-python3-3.14.6/lib /nix/store/ijxpaqwf094rf3iyfw3s5imii25mr3mv-elfutils-0.195-dev/lib /nix/store/s8bvq48mqjiw3firz3l9wx19h55vldy4-libxcrypt-4.5.2/lib /nix/store/0axpqqmj1rcanmm2i8vzpd8xxwkmq2jf-gcc-15.2.0/lib /nix/store/ias8xacs1h3jy7xgwi2awvim61k2ji6c-glibc-2.42-67/lib /lib',
    osname => 'linux',
    osvers => '6.18.33',
    path_sep => ':',
    privlibexp => '/usr/lib/perl5/5.38.2',
    scriptdir => '/usr/bin',
    sitearchexp => '/usr/lib/perl5/site_perl/5.38.2/x86_64-linux',
    sitelibexp => '/usr/lib/perl5/site_perl/5.38.2',
    so => 'so',
    useithreads => undef,
    usevendorprefix => undef,
    version => '5.38.2',
};
