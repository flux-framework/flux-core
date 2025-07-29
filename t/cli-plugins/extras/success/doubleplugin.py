from flux.cli.plugin import CLIPlugin


class GoodPlugin(CLIPlugin):
    def __init__(self, prog, prefix="diff"):
        super().__init__(prog, prefix=prefix)
        self.add_option("--gpumode", dest="different_gpu", help="I am a valid plugin")
