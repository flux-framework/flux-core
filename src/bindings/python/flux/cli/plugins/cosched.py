import json
from collections import defaultdict
from math import ceil

from flux import Flux
from flux.cli.plugin import CLIPlugin

class CoSchedPlugin(CLIPlugin):
    """Flux CLI plugin for co-scheduling.

    Modifies the jobspec to request slots grouped under a configured
    resource type, e.g. numanode, socket, or ccd.
    To enable this plugin, set the allowed parameter under the cosched key in flux config as true.
    e.g. 
    [cosched]
    allowed=true
    n_way=2
    resource_type="numanode"
    Also the flux resource graph (jgf) should be defined for the plugin to work.
    """
    def __init__(self, prog, prefix=None):
        super().__init__(prog, prefix=prefix)

    def _node_type(self, node):
        metadata = node.get("metadata", {})
        return (
            node.get("type")
            or metadata.get("type")
            or metadata.get("resource", {}).get("type")
        )

    def _load_jgf_graph(self):
        handle = Flux()

        scheduling = handle.conf_get("resource.scheduling")

        if isinstance(scheduling, str):
            with open(scheduling) as f:
                data = json.load(f)
        elif isinstance(scheduling, dict):
            data = scheduling.get("graph")
            if data is None:
                data = handle.conf_get("resource.scheduling.graph")
        else:
            data = handle.conf_get("resource.scheduling.graph")

        if data is None:
            raise ValueError(
                "No resource graph found: expected resource.scheduling "
                "to be a graph path or resource.scheduling.graph to contain JGF"
            )

        if isinstance(data, str):
            data = json.loads(data)

        if "graph" in data:
            return data["graph"]

        if "nodes" in data and "edges" in data:
            return data

        raise ValueError("Invalid scheduling graph format")

    def _build_children(self, graph):
        nodes = {node["id"]: node for node in graph["nodes"]}

        children = defaultdict(list)
        for edge in graph["edges"]:
            children[edge["source"]].append(edge["target"])

        return nodes, children

    def _count_descendant_type(self, root, wanted_type, nodes, children):
        total = 0

        for child_id in children.get(root, []):
            child = nodes[child_id]

            if self._node_type(child) == wanted_type:
                total += 1

            total += self._count_descendant_type(
                child_id,
                wanted_type,
                nodes,
                children,
            )

        return total

    def find_cores_per_resource(self, resource_type):
        graph = self._load_jgf_graph()
        nodes, children = self._build_children(graph)

        counts = []

        for node_id, node in nodes.items():
            if self._node_type(node) == resource_type:
                ncores = self._count_descendant_type(
                    node_id,
                    "core",
                    nodes,
                    children,
                )
                counts.append(ncores)

        if not counts:
            raise ValueError(
                f"No resources of type '{resource_type}' found in scheduling graph"
            )

        unique_counts = set(counts)
        if len(unique_counts) != 1:
            raise ValueError(
                f"Resources of type '{resource_type}' have different core counts: "
                f"{sorted(unique_counts)}"
            )

        return counts[0]

    def modify_jobspec(self, args, jobspec):
        try:
            handle = Flux()
            if handle.conf_get('cosched.allowed') == True:
                if len(jobspec.tasks) != 1:
                    # Multiple slot labels in the same request are not allowed for co-scheduling
                    return
                task_count = jobspec.tasks[0]['count']
                ntasks = 0
                nslots = 1
                label = ""
                per_resource = {}
                for parent, resource, count in jobspec.resource_walk():
                    if parent and parent['type'] != 'slot':
                        # if the jobspec specifies more resources than slots don't bother to co-schedule
                        return
                    if resource['type'] == 'slot':
                        label = resource['label']
                        for ttype, tcount in task_count.items():
                            if ttype == 'per_slot':
                                ntasks = tcount * count
                                nslots = count
                            elif ttype == 'per_resource':
                                for rtype, rcount in tcount.items():
                                    per_resource[rtype] = rcount
                                nslots = count
                            else:
                                ntasks = tcount
                                nslots = count
                    if resource['type'] in per_resource:
                        ntasks += per_resource[resource['type']] * count

                resource_type = handle.conf_get('cosched.resource_type', default='numanode')
                waste_threshold = handle.conf_get('cosched.waste_threshold', default=0.3)
                n = handle.conf_get('cosched.n_way', default=2)
                cores_per_resource = self.find_cores_per_resource(resource_type)
                slots_per_resource = max(1, cores_per_resource // n)
                resource_count = ceil(nslots / slots_per_resource)
                slots_inside_resource = min(slots_per_resource, nslots)
                if cores_per_resource > 0 and ((resource_count * slots_inside_resource) / ntasks - 1) <= waste_threshold:
                    jobspec.resources.clear()
                    jobspec.resources.append({'type': resource_type, 'count': resource_count,
                                               'with': [{'type': 'slot', 'count' : slots_inside_resource,
                                                         'with': [{'type': 'core', 'count': 1}],
                                                         'label': label
                                                        }]
                                            })
                    jobspec.tasks[0]['count'] = {'total': ntasks}
        except KeyError as e:
            print(f"Error in allocation type plugin: {e}")
