#ifndef ONEFLOW_CORE_GRAPH_GRAPH_H_
#define ONEFLOW_CORE_GRAPH_GRAPH_H_

#include "oneflow/core/common/str_util.h"
#include "oneflow/core/graph/node.h"
#include "oneflow/core/persistence/persistent_out_stream.h"

namespace oneflow {

template<typename NodeType, typename EdgeType>
class Graph {
 public:
  OF_DISALLOW_COPY_AND_MOVE(Graph);
  Graph() = default;
  virtual ~Graph() = default;

  // For Each
  void ForEachNode(std::function<void(NodeType*)> NodeHandler) const;
  void ForEachNode(std::function<void(NodeType*)> NodeHandler,
                   std::function<bool(NodeType*)> IsNodeReady) const;
  void TopoForEachNode(std::function<void(NodeType*)> NodeHandler) const;
  void ReverseTopoForEachNode(std::function<void(NodeType*)> NodeHandler) const;
  void ForEachEdge(std::function<void(EdgeType*)> EdgeHandler) const;

  void BfsForEachNode(
      const std::list<NodeType*>& starts,
      const std::function<
          void(NodeType*, const std::function<void(NodeType*)>&)>& ForEachNext,
      const std::function<void(NodeType*)>& Handler) const;

  void TopoForEachNode(
      const std::list<NodeType*>& starts,
      const std::function<void(
          NodeType*, const std::function<void(NodeType*)>&)>& ForEachInNode,
      const std::function<void(
          NodeType*, const std::function<void(NodeType*)>&)>& ForEachOutNode,
      const std::function<void(NodeType*)>& Handler) const;

  // Getters
  const std::unordered_set<NodeType*>& source_nodes() const;
  const std::unordered_set<NodeType*>& sink_nodes() const;
  NodeType* SoleSourceNode() const;
  NodeType* SoleSinkNode() const;
  NodeType* SoleNode() const;
  size_t node_num() const { return nodes_.size(); }
  size_t edge_num() const { return edges_.size(); }
  virtual const char* TypeName() const { return ""; }

  // Setters
  template<typename DerivedNodeType = NodeType>
  DerivedNodeType* NewNode();
  EdgeType* NewEdge();
  void AddAllocatedNode(NodeType*);
  void AddAllocatedEdge(EdgeType*);

  // ToDot
  template<typename StreamT>
  void ToDotWithStream(StreamT& out_stream);
  void ToDotWithFilePath(const std::string& file_path);
  void ToDotWithAutoFilePath();

 private:
  std::vector<std::unique_ptr<NodeType>> nodes_;
  std::vector<std::unique_ptr<EdgeType>> edges_;
};

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::ForEachNode(
    std::function<void(NodeType*)> NodeHandler) const {
  for (auto& x : nodes_) { NodeHandler(x.get()); }
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::ForEachNode(
    std::function<void(NodeType*)> NodeHandler,
    std::function<bool(NodeType*)> IsNodeReady) const {
  std::queue<NodeType*> node_queue;
  HashSet<NodeType*> nodes_pushed;
  for (auto& x : nodes_) {
    if (IsNodeReady(x.get())) {
      node_queue.push(x.get());
      CHECK(nodes_pushed.insert(x.get()).second);
    }
  }
  while (node_queue.empty() == false) {
    NodeType* cur_node = node_queue.front();
    node_queue.pop();
    NodeHandler(cur_node);
    cur_node->ForEachNodeOnInOutEdge([&](NodeType* candidate) {
      if (nodes_pushed.find(candidate) == nodes_pushed.end()
          && IsNodeReady(candidate)) {
        node_queue.push(candidate);
        CHECK(nodes_pushed.insert(candidate).second);
      }
    });
  }
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::TopoForEachNode(
    std::function<void(NodeType*)> NodeHandler) const {
  std::list<NodeType*> starts;
  ForEachNode([&](NodeType* node) {
    if (node->in_edges().empty()) { starts.push_back(node); }
  });
  TopoForEachNode(starts, &NodeType::ForEachNodeOnInEdge,
                  &NodeType::ForEachNodeOnOutEdge, NodeHandler);
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::ReverseTopoForEachNode(
    std::function<void(NodeType*)> NodeHandler) const {
  std::list<NodeType*> starts;
  ForEachNode([&](NodeType* node) {
    if (node->out_edges().empty()) { starts.push_back(node); }
  });
  TopoForEachNode(starts, &NodeType::ForEachNodeOnOutEdge,
                  &NodeType::ForEachNodeOnInEdge, NodeHandler);
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::ForEachEdge(
    std::function<void(EdgeType*)> EdgeHandler) const {
  for (auto& x : edges_) { EdgeHandler(x.get()); }
}

template<typename NodeType, typename EdgeType>
NodeType* Graph<NodeType, EdgeType>::SoleNode() const {
  CHECK_EQ(nodes_.size(), 1);
  return nodes_.front().get();
}

template<typename NodeType, typename EdgeType>
template<typename DerivedNodeType>
DerivedNodeType* Graph<NodeType, EdgeType>::NewNode() {
  DerivedNodeType* ret = new DerivedNodeType;
  AddAllocatedNode(ret);
  return ret;
}

template<typename NodeType, typename EdgeType>
EdgeType* Graph<NodeType, EdgeType>::NewEdge() {
  EdgeType* ret = new EdgeType;
  AddAllocatedEdge(ret);
  return ret;
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::AddAllocatedNode(NodeType* node) {
  nodes_.emplace_back(node);
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::AddAllocatedEdge(EdgeType* edge) {
  edges_.emplace_back(edge);
}

template<typename NodeType, typename EdgeType>
template<typename StreamT>
void Graph<NodeType, EdgeType>::ToDotWithStream(StreamT& out_stream) {
  out_stream << "digraph {\n";
  this->ForEachNode([&](NodeType* node) {
    out_stream << "\"" << node->VisualStr() << "\"\n";
  });
  this->ForEachEdge([&](const EdgeType* edge) {
    out_stream << "\"" << edge->src_node()->VisualStr() << "\" -> "
               << "\"" << edge->dst_node()->VisualStr() << "\""
               << "[label=\"" << edge->VisualStr() << "\"];\n";
  });
  out_stream << "}\n";
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::ToDotWithFilePath(
    const std::string& file_path) {
  std::string dir_name = Dirname(file_path);
  if (!LocalFS()->IsDirectory(dir_name)) {
    LocalFS()->RecursivelyCreateDir(dir_name);
  }
  PersistentOutStream out_stream(LocalFS(), file_path);
  ToDotWithStream(out_stream);
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::ToDotWithAutoFilePath() {
  std::string file_path =
      LogDir() + "/dot/" + TypeName() + "/" + NewUniqueId() + ".dot";
  ToDotWithFilePath(file_path);
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::BfsForEachNode(
    const std::list<NodeType*>& starts,
    const std::function<void(NodeType*, const std::function<void(NodeType*)>&)>&
        ForEachNext,
    const std::function<void(NodeType*)>& Handler) const {
  HashMap<NodeType*, bool> has_queued;
  std::queue<NodeType*> queue;
  for (NodeType* start : starts) {
    queue.push(start);
    has_queued[start] = true;
  }
  while (!queue.empty()) {
    NodeType* cur_node = queue.front();
    queue.pop();
    Handler(cur_node);
    ForEachNext(cur_node, [&](NodeType* next) {
      if (!has_queued[next]) {
        queue.push(next);
        has_queued[next] = true;
      }
    });
  }
}

template<typename NodeType, typename EdgeType>
void Graph<NodeType, EdgeType>::TopoForEachNode(
    const std::list<NodeType*>& starts,
    const std::function<void(NodeType*, const std::function<void(NodeType*)>&)>&
        ForEachInNode,
    const std::function<void(NodeType*, const std::function<void(NodeType*)>&)>&
        ForEachOutNode,
    const std::function<void(NodeType*)>& Handler) const {
  HashMap<NodeType*, bool> has_queued;
  std::queue<NodeType*> queue;
  for (NodeType* start : starts) {
    queue.push(start);
    has_queued[start] = true;
    ForEachInNode(start, [&](NodeType*) { LOG(FATAL) << "not a source"; });
  }
  while (!queue.empty()) {
    NodeType* cur_node = queue.front();
    queue.pop();
    Handler(cur_node);
    ForEachOutNode(cur_node, [&](NodeType* out) {
      bool will_be_ready = true;
      ForEachInNode(out, [&](NodeType* in) {
        if (will_be_ready && !has_queued[in]) { will_be_ready = false; }
      });
      if (will_be_ready && !has_queued[out]) {
        queue.push(out);
        has_queued[out] = true;
      }
    });
  }
}

}  // namespace oneflow

#endif  // ONEFLOW_CORE_GRAPH_GRAPH_H_
