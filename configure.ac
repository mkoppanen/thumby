AC_INIT([thumby], [0.0.1], [mikko.koppanen@gmail.com], [thumby], [https://github.com/mkoppanen/thumby])
AC_PREREQ([2.59])

AM_INIT_AUTOMAKE([1.6 foreign subdir-objects])

AC_PROG_CC_C99
AM_PROG_CC_C_O

###############################
# Check for ImageMagick
###############################

AC_WITH(imagemagick, [--with-imagemagick=PATH      ImageMagick install directory]
    [
        if test "$withval" = "yes"; then
            AC_MSG_ERROR(Usage: --with-imagemagick=PATH)
        fi
        IMAGEMAGICK_PATH="$withval"
    ])

AC_MSG_CHECKING(ImageMagick MagickWand API configuration program)

for i in $IMAGEMAGICK_PATH /usr/local /usr; do
    test -r $i/bin/MagickWand-config && WAND_BINARY=$i/bin/MagickWand-config && break
done

if test -z "$WAND_BINARY"; then
for i in $IMAGEMAGICK_PATH /usr/local /usr;
do
  test -r $i/bin/Wand-config && WAND_BINARY=$i/bin/Wand-config && break
done
fi

if test -z "$WAND_BINARY"; then
    AC_MSG_ERROR(not found. Please provide a path to MagickWand-config or Wand-config program.)
fi

AC_MSG_RESULT(found in $WAND_BINARY)

IMAGEMAGICK_CFLAGS=`$WAND_BINARY --cflags`
IMAGEMAGICK_LIBS=`$WAND_BINARY --libs`

CFLAGS="$IMAGEMAGICK_CFLAGS $CFLAGS"
LIBS="$IMAGEMAGICK_LIBS $LIBS"

###############################
# Check for libevent
###############################
LIBEVENT_PATH="/usr/lib/pkgconfig:/usr/local/lib/pkgconfig:/opt/lib/pkgconfig:/opt/local/lib/pkgconfig"

AC_ARG_WITH(libevent,
        [ --with-libevent=PATH      libevent install directory], [
            if test "$withval" = "yes"; then
                AC_MSG_ERROR(Usage: --with-libevent=PATH)
            fi
            LIBEVENT_PATH="$withval"
        ])

AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
if test "x$PKG_CONFIG" = "xno"; then
    AC_MSG_RESULT([pkg-config not found])
    AC_MSG_ERROR([Please reinstall the pkg-config distribution])
fi

ORIG_PKG_CONFIG_PATH="$PKG_CONFIG_PATH"
export PKG_CONFIG_PATH="$LIBEVENT_PATH:$LIBEVENT_PATH/lib/pkgconfig"

AC_MSG_CHECKING([for libevent])
if $PKG_CONFIG --exists libevent; then
    LIBEVENT_CFLAGS=`$PKG_CONFIG libevent --cflags`
    LIBEVENT_LIBS=`$PKG_CONFIG libevent --libs`

    CFLAGS="$LIBEVENT_CFLAGS $CFLAGS"
    LIBS="$LIBEVENT_LIBS $LIBS"

    AC_MSG_RESULT([yes])
else
    AC_MSG_RESULT([not found])
    AC_MSG_ERROR([Please reinstall the libevent distribution])
fi

PKG_CONFIG_PATH="$ORIG_PKG_CONFIG_PATH"

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
