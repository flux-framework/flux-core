AC_DEFUN([X_AC_ENABLE_SANITIZER], [
    AC_MSG_CHECKING([whether to enable a sanitizer tool])
    m4_define([san_options], [@<:@OPT=no/address/thread@:>@])
    m4_define([cc_san], [The selected compiler does not support sanitizers])
    m4_define([cc_san_sup], [GCC >= 4.8 or Clang >= 3.5])
    AC_ARG_ENABLE([sanitizer],
        [AS_HELP_STRING(--enable-sanitizer@<:@=OPT@:>@,
            enable a sanitizer tool @<:@default=address@:>@ san_options)],
        [san_enabled=$enableval],
        [san_enabled="check"])    

    if test "x$san_enabled" = "xcheck"; then
        san_enabled="no"
    elif test "x$san_enabled" = "xyes"; then
        san_enabled="address"
    fi
    AC_MSG_RESULT($san_enabled)

    if test "x$san_enabled" = "xaddress" -o "x$san_enabled" = "xthread" ; then 
        CFLAGS="$CFLAGS -fsanitize=$san_enabled -fno-omit-frame-pointer -fsanitize-recover=all"
        LDFLAGS="$LDFLAGS -fsanitize=$san_enabled"
        AC_MSG_CHECKING([whether CC supports -fsanitizer=$san_enabled and -fsanitize-recover=all])
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
            [AC_MSG_RESULT([yes])],
            [AC_MSG_RESULT([no]) 
             AC_MSG_ERROR(cc_san. Please use cc_san_sup.)])
        AS_VAR_SET(san_ld_zdef_flag, [])
        AC_SUBST(san_ld_zdef_flag)
    elif test "x$san_enabled" = "xno" ; then
        AS_VAR_SET(san_ld_zdef_flag, [-no-undefined])
        AC_SUBST(san_ld_zdef_flag)
    else
        AC_MSG_ERROR($san_enabled is a unsupported option.)
    fi
])
