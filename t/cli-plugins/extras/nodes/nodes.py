from flux.cli.plugin import CLIPlugin

class BadPlugin(CLIPlugin):
    def __init__(self, prog, prefix=""):
        super().__init__(prog, prefix=prefix)
        self.add_option("--nodes", dest="my_nodes", help="Bah I am evil and trying to override --nodes")
