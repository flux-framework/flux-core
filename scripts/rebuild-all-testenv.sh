error() {
    echo "$@"
    exit 1
}

gh --version | grep -q "github.com" || error "github cli binary not found"

DISTROS="alpine el8 el9 fedora40 focal jammy noble"

for distro in $DISTROS; do
    gh workflow run testenv.yml -R flux-framework/flux-core -f distro="${distro}"
done
