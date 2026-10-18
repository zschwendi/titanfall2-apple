#!/bin/sh
# The link-level half of "no socket of the guest reaches the network" (rt/hostsock.h, THE RULE).
#
#     runtime/tools/host_net_lint.sh --archive build/runtime/librt.a
#     runtime/tools/host_net_lint.sh --program build/gate3/gate3
#     runtime/tools/host_net_lint.sh --program FILE --dxmt-loader OBJECT --inputs LIST --dxmt-libs "FILE ..."
#     runtime/tools/host_net_lint.sh --self-test <cc> <scratch dir>     the lint's own test
#
# The DXMT loader (decided by the owner, 12 October: "allow the dlopen and dlsym for the loader").
# The renderer's back end loads DXMT's libraries with dlopen and dlsym, so with the three options
# above, and only then, dlopen and dlsym in FILE are accepted, on these conditions: OBJECT is the one
# input that asks for them (it must ask for both); no other input of LIST (one path per line: every
# other object and archive linked into FILE) asks for them; every file of --dxmt-libs passes the
# rule of --program in its strict form (no socket, resolver or interface function, no dlopen, dlsym
# or syscall; getsockname and getpeername are not exempt there).  syscall stays forbidden in FILE.
#
# Reads the undefined symbols (what the code asks of the host's libraries; `nm -u`) and fails if
# any is a socket, resolver or network-interface function of the host, or a way to reach one
# without naming it (dlopen, dlsym, syscall).  The two exceptions are getsockname and
# getpeername, which only read: in an archive they may be asked for by core/hostsock.o alone
# (the census); in a linked program they may be present.  A program that has none of the rest
# cannot open, bind, connect or send on a socket by any path the linker knows of.
#
# Not covered, and said so: a program that starts another program (the driver starts itself
# again: posix_spawn of its own executable, gate3/run.c).  Exit 0 = clean, 1 = a name found.
set -u
mode=${1:-}
file=${2:-}

# The lint's own test: an object that asks for socket() and sendto() must fail, in an archive and
# in a linked program, also when it is named hostsock.o; one that asks only for getsockname must
# pass as hostsock.o and fail under any other name.  The objects are compiled, never run.
if [ "$mode" = "--self-test" ]; then
    cc=$file
    dir=${3:-}
    [ -n "$cc" ] && [ -n "$dir" ] || { echo "usage: $0 --self-test <cc> <scratch dir>" >&2; exit 2; }
    rm -rf "$dir" && mkdir -p "$dir" || exit 2
    printf 'int socket(int, int, int);\nlong sendto(int, const void *, unsigned long, int, const void *, unsigned);\nint opens(void) { int s = socket(2, 2, 0); return (int)sendto(s, "", 0, 0, 0, 0); }\n' > "$dir/opens.c"
    printf 'int getsockname(int, void *, unsigned *);\nint reads(int fd, void *p, unsigned *n) { return getsockname(fd, p, n); }\n' > "$dir/reads.c"
    printf 'int opens(void);\nint main(void) { return opens(); }\n' > "$dir/main.c"
    printf 'int main(void) { return 0; }\n' > "$dir/none.c"
    fail() { echo "host-net-lint: SELF-TEST FAILED: $1"; exit 1; }
    $cc -c "$dir/opens.c" -o "$dir/opens.o" && $cc -c "$dir/reads.c" -o "$dir/reads.o" && $cc -c "$dir/main.c" -o "$dir/main.o" \
        && $cc "$dir/main.o" "$dir/opens.o" -o "$dir/prog-opens" && $cc "$dir/none.c" -o "$dir/prog-none" || fail "the compiler did not make the test objects"
    cp "$dir/opens.o" "$dir/hostsock.o"
    ar rcs "$dir/opens.a" "$dir/opens.o";           sh "$0" --archive "$dir/opens.a" > /dev/null && fail "an object that asks for socket and sendto passed"
    ar rcs "$dir/opens-named.a" "$dir/hostsock.o";  sh "$0" --archive "$dir/opens-named.a" > /dev/null && fail "socket and sendto passed in an object named hostsock.o"
    ar rcs "$dir/reads.a" "$dir/reads.o";           sh "$0" --archive "$dir/reads.a" > /dev/null && fail "getsockname passed outside hostsock.o"
    cp "$dir/reads.o" "$dir/hostsock.o"
    ar rcs "$dir/reads-named.a" "$dir/hostsock.o";  sh "$0" --archive "$dir/reads-named.a" > /dev/null || fail "getsockname in hostsock.o did not pass"
    sh "$0" --program "$dir/prog-opens" > /dev/null && fail "a linked program that asks for socket and sendto passed"
    sh "$0" --program "$dir/prog-none" > /dev/null || fail "a linked program that asks for nothing did not pass"
    # The DXMT loader: dl.o is the loader (asks for dlopen and dlsym), dl2.o a second input that asks for dlopen,
    # sys.o asks for syscall, and the libraries are none.o (asks for nothing) and opens.o (asks for socket).
    printf 'void *dlopen(const char *, int);\nvoid *dlsym(void *, const char *);\nvoid *loadit(void) { return dlsym(dlopen("x", 0), "y"); }\n' > "$dir/dl.c"
    printf 'void *loadit(void);\nint main(void) { return loadit() != 0; }\n' > "$dir/dlmain.c"
    printf 'void *dlopen(const char *, int);\nvoid *loadit2(void) { return dlopen("y", 0); }\n' > "$dir/dl2.c"
    printf 'long syscall(int, ...);\nlong sysuse(void) { return syscall(0); }\n' > "$dir/sys.c"
    $cc -c "$dir/dl.c" -o "$dir/dl.o" && $cc -c "$dir/dlmain.c" -o "$dir/dlmain.o" && $cc -c "$dir/dl2.c" -o "$dir/dl2.o" \
        && $cc -c "$dir/sys.c" -o "$dir/sys.o" && $cc -c "$dir/none.c" -o "$dir/none.o" \
        && $cc "$dir/dlmain.o" "$dir/dl.o" -o "$dir/prog-dl" && $cc -Wl,-undefined,dynamic_lookup "$dir/dlmain.o" "$dir/dl.o" "$dir/sys.o" -o "$dir/prog-dl-sys" \
        || fail "the compiler did not make the DXMT test objects"
    printf '%s\n' "$dir/dlmain.o" > "$dir/list-main"
    printf '%s\n' "$dir/dlmain.o" "$dir/dl2.o" > "$dir/list-two"
    sh "$0" --program "$dir/prog-dl" > /dev/null && fail "a program that asks for dlopen passed without the DXMT loader option"
    sh "$0" --program "$dir/prog-dl" --dxmt-loader "$dir/dl.o" --inputs "$dir/list-main" --dxmt-libs "$dir/none.o" > /dev/null || fail "a program whose dlopen and dlsym are the loader's alone did not pass"
    sh "$0" --program "$dir/prog-dl" --dxmt-loader "$dir/dl.o" --inputs "$dir/list-two" --dxmt-libs "$dir/none.o" > /dev/null && fail "a second input that asks for dlopen passed"
    sh "$0" --program "$dir/prog-dl" --dxmt-loader "$dir/none.o" --inputs "$dir/list-main" --dxmt-libs "$dir/none.o" > /dev/null && fail "a named loader that does not ask for dlopen passed"
    sh "$0" --program "$dir/prog-dl" --dxmt-loader "$dir/dl.o" --inputs "$dir/list-main" --dxmt-libs "$dir/opens.o" > /dev/null && fail "a DXMT library that asks for socket passed"
    sh "$0" --program "$dir/prog-dl-sys" --dxmt-loader "$dir/dl.o" --inputs "$dir/list-main" --dxmt-libs "$dir/none.o" > /dev/null && fail "a program that also asks for syscall passed"
    rm -rf "$dir"
    echo "host-net-lint: self-test ok: 12 cases (socket and sendto fail in an archive, under the census's name and in a program; getsockname passes only in hostsock.o; an empty program passes; with the DXMT loader option dlopen passes only for the named loader, and fails without the option, with a second asker, with a loader that does not ask, with a library that asks for socket, and with syscall)"
    exit 0
fi

if [ -z "$file" ] || { [ "$mode" != "--archive" ] && [ "$mode" != "--program" ]; }; then
    echo "usage: $0 --archive <lib.a> | --program <linked program>" >&2
    exit 2
fi
[ -f "$file" ] || { echo "host-net-lint: no file $file" >&2; exit 2; }

names='socket|socketpair|bind|connect|connectx|disconnectx|listen|accept|send|sendto|sendmsg|sendmsg_x|sendfile'
names="$names|recv|recvfrom|recvmsg|recvmsg_x|getsockname|getpeername|getsockopt|setsockopt|shutdown"
names="$names|getaddrinfo|freeaddrinfo|getnameinfo|gethostbyname|gethostbyname2|gethostbyaddr|gethostent|gethostname"
names="$names|getifaddrs|freeifaddrs|if_nameindex|if_nametoindex|getservbyname|getservbyport|getprotobyname"
names="$names|res_[A-Za-z0-9_]*|dns_[A-Za-z0-9_]*|DNSService[A-Za-z0-9_]*|nw_[A-Za-z0-9_]*"
names="$names|CFSocket[A-Za-z0-9_]*|CFStream[A-Za-z0-9_]*|CFHost[A-Za-z0-9_]*|CFNetwork[A-Za-z0-9_]*|CFReadStream[A-Za-z0-9_]*|CFWriteStream[A-Za-z0-9_]*"
names="$names|SCNetwork[A-Za-z0-9_]*"
names_loader="$names|syscall|__syscall"      # the DXMT loader form of --program: dlopen and dlsym are its exception
names="$names_loader|dlopen|dlsym"

# The options of the DXMT loader form (only with --program; all three or none).
dxmt=0; loader=; inputs=; libs=
if [ "$mode" = "--program" ]; then
    shift 2
    while [ $# -gt 0 ]; do
        case $1 in
            --dxmt-loader|--inputs|--dxmt-libs)
                [ $# -ge 2 ] || { echo "host-net-lint: $1 needs a value" >&2; exit 2; }
                case $1 in --dxmt-loader) loader=$2 ;; --inputs) inputs=$2 ;; *) libs=$2 ;; esac
                dxmt=1; shift 2 ;;
            *) echo "usage: $0 --program FILE [--dxmt-loader OBJECT --inputs LIST --dxmt-libs \"FILE ...\"]" >&2; exit 2 ;;
        esac
    done
    if [ "$dxmt" = 1 ] && { [ -z "$loader" ] || [ -z "$inputs" ] || [ -z "$libs" ]; }; then
        echo "host-net-lint: the DXMT loader form needs all of --dxmt-loader, --inputs and --dxmt-libs" >&2; exit 2
    fi
fi

if [ "$mode" = "--archive" ]; then
    # `nm -u -A lib.a` prints "lib.a:member.o: _name".
    found=$(nm -u -A "$file" 2>/dev/null | grep -E ": _($names)\$" | grep -v -E ':hostsock\.o: _(getsockname|getpeername)$')
    members=$(nm -u -A "$file" 2>/dev/null | sed -n 's/^[^:]*:\([^:]*\): .*/\1/p' | sort -u | wc -l | tr -d ' ')
    what="$members objects of $file"
else
    pnames=$names
    if [ "$dxmt" = 1 ]; then
        pnames=$names_loader
        nm -u "$file" > /dev/null 2>&1 || { echo "host-net-lint: FAILED: cannot read the undefined symbols of $file"; exit 1; }
    fi
    found=$(nm -u "$file" 2>/dev/null | grep -E "^_($pnames)\$" | grep -v -E '^_(getsockname|getpeername)$')
    what="$file"
fi
if [ -n "$found" ]; then
    echo "host-net-lint: FAILED: $what asks the host for a network function (rt/hostsock.h, THE RULE):"
    echo "$found" | sed 's/^/    /'
    exit 1
fi
if [ "$dxmt" = 1 ]; then
    # The loader: it must ask for both, and be the only input that does.
    [ -f "$loader" ] || { echo "host-net-lint: FAILED: no DXMT loader object $loader"; exit 1; }
    lo=$(nm -u -A "$loader" 2>&1) || { echo "host-net-lint: FAILED: cannot read the undefined symbols of the DXMT loader $loader"; exit 1; }
    for s in dlopen dlsym; do
        printf '%s\n' "$lo" | grep -q -E ": _$s\$" || { echo "host-net-lint: FAILED: the DXMT loader $loader does not ask for $s, so the exception would be pinned to the wrong object (rt/hostsock.h, THE RULE)"; exit 1; }
    done
    # Every other input: no dlopen, no dlsym.
    [ -f "$inputs" ] || { echo "host-net-lint: FAILED: no list of inputs $inputs"; exit 1; }
    n=0
    while IFS= read -r p || [ -n "$p" ]; do
        case $p in *[![:space:]]*) ;; *) continue ;; esac
        [ -f "$p" ] || { echo "host-net-lint: FAILED: input $p (in $inputs) is not a file"; exit 1; }
        out=$(nm -u -A "$p" 2>&1) || { echo "host-net-lint: FAILED: cannot read the undefined symbols of input $p"; exit 1; }
        hit=$(printf '%s\n' "$out" | grep -E ': _(dlopen|dlsym)$')
        if [ -n "$hit" ]; then
            echo "host-net-lint: FAILED: $p asks for dlopen or dlsym, and only the DXMT loader $loader may (rt/hostsock.h, THE RULE):"
            echo "$hit" | sed 's/^/    /'
            exit 1
        fi
        n=$((n+1))
    done < "$inputs"
    [ "$n" -gt 0 ] || { echo "host-net-lint: FAILED: the list of inputs $inputs names no file"; exit 1; }
    # The DXMT libraries the loader loads: the rule of --program in its strict form.
    set -f
    nl=0
    for l in $libs; do
        [ -f "$l" ] || { echo "host-net-lint: FAILED: no DXMT library $l"; exit 1; }
        out=$(nm -u -A "$l" 2>&1) || { echo "host-net-lint: FAILED: cannot read the undefined symbols of DXMT library $l"; exit 1; }
        hit=$(printf '%s\n' "$out" | grep -E ": _($names)\$")
        if [ -n "$hit" ]; then
            echo "host-net-lint: FAILED: DXMT library $l asks the host for a network, dlopen, dlsym or syscall function (rt/hostsock.h, THE RULE):"
            echo "$hit" | sed 's/^/    /'
            exit 1
        fi
        nl=$((nl+1))
    done
    set +f
    [ "$nl" -gt 0 ] || { echo "host-net-lint: FAILED: no DXMT library is named in --dxmt-libs"; exit 1; }
    echo "host-net-lint: ok: $file: the DXMT loader exception was used: $loader asks for dlopen and dlsym, the only one of $n inputs that does; $nl DXMT libraries scanned, none asks for a socket, resolver, interface, dlopen, dlsym or syscall function"
    exit 0
fi
echo "host-net-lint: ok: $what: no socket, resolver or interface function of the host is asked for (getsockname and getpeername, which only read, by the census alone)"
