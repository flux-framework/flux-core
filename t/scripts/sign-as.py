import sys

from flux.security import SecurityContext

if len(sys.argv) < 2:
    print("Usage: {0} USERID".format(sys.argv[0]))
    sys.exit(1)

userid = int(sys.argv[1])
ctx = SecurityContext()
payload = sys.stdin.read()

print(ctx.sign_wrap_as(userid, payload, mech_type="none").decode("utf-8"))
# vi: ts=4 sw=4 expandtab
