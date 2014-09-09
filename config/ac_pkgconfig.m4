# Thanks to Arnaud Quette for this bit of m4

AC_DEFUN([AC_PKGCONFIG],
[
    pkgconfigdir='${libdir}/pkgconfig'
    AC_MSG_CHECKING(whether to install pkg-config *.pc files)
    AC_ARG_WITH(pkgconfig-dir,
     AC_HELP_STRING([--with-pkgconfig-dir=PATH], [where to install pkg-config *.pc files (EPREFIX/lib/pkgconfig)]),
     [
        case "${withval}" in
            yes|auto)
            ;;
            no)
                pkgconfigdir=""
            ;;
            *)
                pkgconfigdir="${withval}"
            ;;
        esac
     ],
     )
    if test -n "${pkgconfigdir}"; then
      AC_MSG_RESULT(${pkgconfigdir})
    else
      AC_MSG_RESULT(no)
    fi
    AM_CONDITIONAL(WITH_PKG_CONFIG, test -n "${pkgconfigdir}")
    AC_SUBST(pkgconfigdir)
])
