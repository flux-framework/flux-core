from flux.cli.plugin import CLIPlugin


class BadPlugin(CLIPlugin):
    def __init__(self, prog):
        super().__init__(prog)
        self.add_option("--gpumode", help="I want to mess up the gpumode")
