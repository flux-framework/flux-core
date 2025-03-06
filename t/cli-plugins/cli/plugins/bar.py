import flux
from flux.cli.plugin import CLIPlugin


class CustomFluxionPolicyPlugin(CLIPlugin):
    """Accept command-line option that updates fluxion policy in subinstance"""

    def __init__(self, prog):
        super().__init__(prog)
        self.opt = "match-policy"
        self.help = "Set the sched-fluxion-resource.match-policy for a subinstance. See sched-fluxion-resource(5) for available policies."

    def preinit(self, args, values):
        pol = None
        try:
            pol = str(values["match-policy"])
        except KeyError:
            pass
        if self.prog in ("batch", "alloc") and pol:
            args.conf.update(f'sched-fluxion-resource.match-policy="{pol}"')

    def modify_jobspec(self, args, jobspec, values):
        try:
            pol = str(values["match-policy"])
        except KeyError:
            pol = None
        else:
            if pol:
                jobspec.setattr("system.fluxion_match_policy", str(pol))

    def validate(self, jobspec):
        raise ValueError("hi hobbs")
        try:
            pol = jobspec.attributes["system"]["fluxion_match_policy"]
        except KeyError:
            return
        if pol != "firstnodex":
            raise ValueError("Invalid option for fluxion-match-policy")
        return
