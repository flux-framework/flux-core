
uses "Node"
uses "ClusterOf"

Hierarchy "default" {
    Resource{ "cluster", name = "hype",
    children = ClusterOf{
                  type = Node,
                  ids = "201-354",
                  args = { name = "hype", sockets = {"0-7", "8-15"} }
              }
    }
}
