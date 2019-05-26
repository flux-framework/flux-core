@@
identifier ret;
identifier pack_func =~ "^flux_.*_pack$";
@@

+// REMOVE ME - INSERTED BY COCCI
+// clang-format off
ret = pack_func(...);
+// REMOVE ME - INSERTED BY COCCI
+// clang-format on

@@
identifier unpack_func =~ "^flux_.*_unpack$";
@@

+// REMOVE ME - INSERTED BY COCCI
+// clang-format off
if(unpack_func(...)<0) {
+// REMOVE ME - INSERTED BY COCCI
+// clang-format on
...
}

@@
type struct_type;
identifier name;
@@

+// REMOVE ME - INSERTED BY COCCI
+// clang-format off
struct_type name[] = {
...
};
+// REMOVE ME - INSERTED BY COCCI
+// clang-format on
