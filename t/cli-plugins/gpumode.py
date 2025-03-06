from flux.cli.plugin import CLIPlugin


class RediscoverGPUPlugin(CLIPlugin):
    """Add --multi-user option to configure a multi-user subinstance"""

    def __init__(self, prog):
        super().__init__(prog)
        
    def add_options(self, registry):
        registry.add_option(
            opt="gpumode",
            usage="Option for setting AMD SMI compute partitioning. Choices are CPX, TPX, or SPX.",
        )
        registry.add_option(opt="foo", usage="Enable bar")

    def preinit(self, args, plugin_args):
        if self.prog in ("batch", "alloc"):
            gpumode = plugin_args.gpumode
            if gpumode == "TPX" or gpumode == "CPX":
                args.conf.update("resource.rediscover=true")
            elif gpumode == "SPX" or gpumode is None:
                pass
            else:
                raise ValueError("--gpumode can only be set to CPX, TPX, or SPX")

    def modify_jobspec(self, args, jobspec, plugin_args):
        if plugin_args.gpumode:
            jobspec.setattr("gpumode", plugin_args.gpumode)
