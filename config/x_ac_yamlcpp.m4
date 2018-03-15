AC_DEFUN([X_AC_YAMLCPP], [

    AC_ARG_ENABLE([jobspec],
        AS_HELP_STRING([--disable-jobspec],
            [Disable compilation of jobspec library]))

    AS_IF([test "x$enable_jobspec" != "xno"], [

        PKG_CHECK_MODULES([YAMLCPP], [yaml-cpp >= 0.5.1],
                          ,
			  [AC_MSG_ERROR(dnl
[Required yaml-cpp package version not installed.
Add --disable-jobspec, or set the PKG_CONFIG_PATH env var appropriately.])])

        ac_save_LIBS="$LIBS"
        LIBS="$LIBS $YAMLCPP_LIBS"
        ac_save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $YAMLCPP_CFLAGS"
        AC_LANG_PUSH([C++])

        AC_MSG_CHECKING([whether yaml-cpp/yaml.h is usable])
        AC_PREPROC_IFELSE(
            [AC_LANG_PROGRAM([#include <yaml-cpp/yaml.h>], [])],
                [AC_MSG_RESULT([yes])],
                [AC_MSG_FAILURE([yaml-cpp/yaml.h doesn't appear to be usable.])]
        )

        # YAML::Node::Mark() is only present in yaml-cpp beginning
        # with release 0.5.3.
        AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <yaml-cpp/yaml.h>],
                                        [YAML::Node node; node.Mark()])],
                       [AC_DEFINE([HAVE_YAML_MARK], [1],
                                  [Define to 1 if yaml-cpp library has YAML::Node::Mark()])],
                       [])

        AC_LANG_POP([C++])
        LIBS="$ac_save_LIBS"
        CFLAGS="$ac_save_CFLAGS"
    ])

    AM_CONDITIONAL([ENABLE_JOBSPEC], [test "x$enable_jobspec" != "xno"])
  ]
)
