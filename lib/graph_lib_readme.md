# GRAPH - nodes, links, and the questions that follow

`lib/graph.jdb` holds a graph as a map that the calls change in place.
Nodes are named by strings and keep the order they were added in, so
every answer here comes out the same on every run: the walks, the
groups, the order of work and the way through are all reproducible.

Stands in for: networkx (the everyday part).

## Quick start

```basic
IMPORT GRAPH

DIM build = GRAPH.NEW({"directed": TRUE})
GRAPH.LINK(build, "lexer", "parser")
GRAPH.LINK(build, "parser", "codegen")
PRINT JOIN(GRAPH.TOPO(build), " then ")

DIM roads = GRAPH.FROMEDGES([["harbour", "mill", 3], ["mill", "market", 9]])
DIM route = GRAPH.PATH(roads, "harbour", "market")
PRINT route{"cost"}; " miles through "; JOIN(route{"nodes"}, " -> ")
```

## Making one

| Call | What it does |
|------|--------------|
| `NEW([opts])` | An empty graph. `directed` (FALSE) decides whether a link goes both ways. |
| `FROMEDGES(edges, [opts])` | A graph from an array of links, each `[from, to]` or `[from, to, weight]`. |
| `FROMADJ(names, matrix, [opts])` | A graph from names and a square matrix of weights, where a zero means no link. |

## Changing one

| Call | What it does |
|------|--------------|
| `ADD(g, name$)` | Adds a node on its own. A link adds its ends by itself. |
| `LINK(g, a$, b$, [weight])` | Links two nodes, weight 1 by default. Linking the same pair again only changes the weight. |
| `UNLINK(g, a$, b$)` | Takes the link out. |
| `DROP(g, name$)` | Takes a node and every link that touches it out. |

## Asking about one

| Call | What it answers |
|------|-----------------|
| `NODES(g)` / `EDGES(g)` | The nodes in the order they were added; the links as `[from, to, weight]`, each pair named once. |
| `HAS(g, name$)` / `LINKED(g, a$, b$)` | Whether the node or the link is there. |
| `WEIGHT(g, a$, b$)` | The weight of a link, zero when there is none. |
| `NEIGHBOURS(g, name$)` / `SOURCES(g, name$)` | Where its links lead; where they come from. |
| `DEGREE / OUTDEGREE / INDEGREE(g, name$)` | How many links touch it, lead out of it, lead into it. |
| `ORDER(g)` / `SIZE(g)` | How many nodes; how many links. |
| `DENSITY(g)` | The share of the links that could exist that do, from zero to one. |
| `ISDIRECTED(g)` | Whether links point one way. |
| `ADJ(g)` | The weights as a square matrix in the order `NODES` gives. |

## Walking it

| Call | What it answers |
|------|-----------------|
| `BFS(g, start$)` | The nodes reached, nearest first. |
| `DFS(g, start$)` | The same nodes, following each branch to its end first. |
| `REACHES(g, a$, b$)` | Whether a walk leads from one to the other. |
| `COMPONENTS(g)` | The groups of nodes joined to each other, ignoring which way a link points. |
| `CYCLE(g)` | The nodes of one cycle, the first repeated at the end, or an empty array. |
| `ISDAG(g)` | Whether the graph is directed and has no cycle. |
| `TOPO(g)` | The nodes in an order where every link points forwards. |

`TOPO` raises when the graph is not directed, and when it has a cycle it
names the nodes on it:

```
GRAPH.TOPO: the graph has a cycle through docs -> grammar -> lexer -> docs
```

## The way through

| Call | What it answers |
|------|-----------------|
| `PATH(g, a$, b$)` | The cheapest walk: `found nodes cost hops`. |
| `ASTAR(g, a$, b$, guess)` | The same, with a guess of what is left to pay. |
| `DIST(g, from$)` | The cost of reaching every node, as a map. |
| `ALLPATHS(g, a$, b$, [max_hops])` | Every walk that visits no node twice, the shortest first. |

`PATH` and `DIST` are Dijkstra, so the weights must not be negative. The
guess `ASTAR` takes is called with the node and the target and must
never ask for more than the real remainder; a straight-line distance
between two points on a map is the usual one:

```basic
FUNC AsCrow(here$, goal$)
    DIM a = towns{here$}
    DIM b = towns{goal$}
    RETURN SQR((a[0] - b[0]) ^ 2 + (a[1] - b[1]) ^ 2)
ENDFUNC

DIM route = GRAPH.ASTAR(roads, "harbour", "market", AsCrow@)
```

## Writing it out

| Call | What it does |
|------|--------------|
| `DOT$(g, [opts])` | The graph as Graphviz text. `name`, `weights` (TRUE), `rankdir`. |

```
jdBasic pipeline.jdb > pipeline.dot
dot -Tpng pipeline.dot -o pipeline.png
```

## Notes

- The search is a plain scan over the nodes still to settle, which is
  the right shape up to a few thousand nodes and keeps the order of the
  answers fixed.
- `COMPONENTS` on a directed graph gives the groups that are joined when
  the direction is ignored.
- A self link is allowed and counts once.
- The self test compares `PATH` and `DIST` against the graph the
  textbooks use for Dijkstra, and sorts the library's own build order.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/graph_selftest.jdb`. Demo: `jdb/demos/jdlibs/graph_demo.jdb`.
