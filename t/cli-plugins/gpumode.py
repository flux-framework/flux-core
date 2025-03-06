from flux.cli.plugin import CLIPlugin


class RediscoverGPUPlugin(CLIPlugin):
    """Add --multi-user option to configure a multi-user subinstance"""

    def __init__(self, prog):
        super().__init__(prog)
        
    def add_options(self, parser):
        parser.add_argument("--gpumode", help="Option for setting AMD SMI compute partitioning. Choices are CPX, TPX, or SPX.")
        parser.add_argument("--foo", help="Enable bar")

    def preinit(self, args):
        if self.prog in ("batch", "alloc") and args.gpumode:
            gpumode = args.gpumode
            if gpumode == "TPX" or gpumode == "CPX":
                args.conf.update("resource.rediscover=true")
            elif gpumode == "SPX" or gpumode is None:
                pass
            else:
                raise ValueError("--gpumode can only be set to CPX, TPX, or SPX")

    def modify_jobspec(self, args, jobspec):
        if args.gpumode:
            jobspec.setattr("gpumode", args.gpumode)
