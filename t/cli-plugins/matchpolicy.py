from flux.cli.plugin import CLIPlugin


class CustomFluxionPolicyPlugin(CLIPlugin):
    """Accept command-line option that updates fluxion policy in subinstance"""

    def __init__(self, prog):
        super().__init__(prog)
        
    def add_options(self, parser):
        parser.add_argument('--match-policy', help='Set the sched-fluxion-resource.match-policy for a subinstance. See sched-fluxion-resource(5) for available policies.',)

    def preinit(self, args):
        pol = args.match_policy
        if self.prog in ("batch", "alloc") and pol:
            args.conf.update(f'sched-fluxion-resource.match-policy="{pol}"')

    def modify_jobspec(self, args, jobspec):
        jobspec.setattr("system.fluxion_match_policy", str(args.match_policy))

    def validate(self, jobspec):
        try:
            pol = jobspec.attributes["system"]["fluxion_match_policy"]
        except KeyError:
            pol = "None"
        if pol != "firstnodex" and pol != "None":
            raise ValueError(f"Invalid option for fluxion-match-policy: {pol}")
