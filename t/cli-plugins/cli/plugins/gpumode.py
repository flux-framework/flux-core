from flux.cli.plugin import CLIPlugin


class RediscoverGPUPlugin(CLIPlugin):
    """Add --multi-user option to configure a multi-user subinstance"""

    def __init__(self, prog):
        super().__init__(prog)
        self.opt = "gpumode"
        self.usage = "set gpumode on elcap"
        self.help = "Option for setting AMD SMI compute partitioning. Choices are CPX, TPX, or SPX."
        self.version = str("0.1.0")

    def preinit(self, args, value):
        if self.prog in ("batch", "alloc"):
            try:
                gpumode = str(value["gpumode"])
            except KeyError:  ## necessary?
                gpumode = ""
            if gpumode == "TPX" or gpumode == "CPX":
                args.conf.update("resource.rediscover=true")
            elif gpumode == "SPX" or gpumode == "":
                pass
            else:
                raise ValueError("--gpumode can only be set to CPX, TPX, or SPX")

    def modify_jobspec(self, args, jobspec, value):
        try:
            if value["gpumode"]:
                jobspec.setattr("gpumode", value["gpumode"])
        except KeyError:
            pass

    def validate(self, jobspec):
        return
