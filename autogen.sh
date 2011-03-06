#!/bin/sh

autogen_run_all() {
    touch_if_not_found COPYING INSTALL NEWS README AUTHORS ChangeLog
    run aclocal ${ACLOCAL_ARGS}
    if [ ! -d config ]; then
        mkdir config
    fi
    run libtoolize --copy --force
    run autoheader
    run automake --add-missing --foreign --copy
    run autoconf
}

run() {
    $@
    if [ $? -ne 0 ]; then
        echo "Failed $@"
        exit 1
    fi
}

touch_if_not_found() {
    for f in $*; do
        if [ ! -f $f ]; then
            touch $f
        fi
    done
}

autogen_clean() {
    if [ -f Makefile ]; then
        make clean
    fi
    rm -rf aclocal.m4 autom4te.cache config config.log config.status \
        configure libtool Makefile Makefile.in \
        src/.deps src/config.h src/config.h.in src/Makefile src/Makefile.in \
        src/stamp-h1 \
        test/.deps test/Makefile test/Makefile.in
#    rm_if_empty COPYING INSTALL NEWS README AUTHORS ChangeLog
}

rm_if_empty() {
    for f in $*; do
        if [ -f $f -a ! -s $f ]; then
            rm $f
        fi
    done
}

case "_$1" in
_)
    autogen_run_all
    ;;
_clean)
    autogen_clean
    ;;
*)
    echo "Usage: $0 [clean]"
    ;;
esac
