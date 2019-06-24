#include "module.h"
#include "modules/api.h"
#include <math.h>
#include <stdlib.h>

void
create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{
  // TODO: streamline the api needed to do things like this!
  // create a box blur node:

  const int wd = module->connector[1].roi.roi_wd;
  const int ht = module->connector[1].roi.roi_ht;
  const int dp = 1;

  // TODO: in fact these should be grey scale
  // TODO dt_node_add(graph, module, "name", "kernel", "entry")
  // all three nodes need this:
  dt_connector_t ci = {
    .name   = dt_token("input"),
    .type   = dt_token("read"),
    .chan   = dt_token("rgba"),
    .format = dt_token("f32"),
    .roi    = module->connector[0].roi,
    .connected_mi = -1,
  };
  dt_connector_t co = {
    .name   = dt_token("output"),
    .type   = dt_token("write"),
    .chan   = dt_token("rgba"),
    .format = dt_token("f32"),
    .roi    = module->connector[1].roi,
  };
  // add node blur1
  assert(graph->num_nodes < graph->max_nodes);
  const int id_blur1 = graph->num_nodes++;
  dt_node_t *node_blur1 = graph->node + id_blur1;
  *node_blur1 = (dt_node_t) {
    .name   = dt_token("blur1"),
    .kernel = dt_token("blur1"),
    .module = module,
    .wd     = wd,
    .ht     = ht,
    .dp     = dp,
    .num_connectors = 2,
    .connector = {
      ci, co,
    },
  };
  // TODO: could run these on downsampled image instead
  // add nodes blur2h and blur2v
  assert(graph->num_nodes < graph->max_nodes);
  const int id_blur2h = graph->num_nodes++;
  dt_node_t *node_blur2h = graph->node + id_blur2h;
  *node_blur2h = (dt_node_t) {
    .name   = dt_token("blur2h"),
    .kernel = dt_token("blur2h"),
    .module = module,
    .wd     = wd,
    .ht     = ht,
    .dp     = dp,
    .num_connectors = 2,
    .connector = {
      ci, co,
    },
    .push_constant_size = 4,
    .push_constant = {2},
  };
  assert(graph->num_nodes < graph->max_nodes);
  const int id_blur2v = graph->num_nodes++;
  dt_node_t *node_blur2v = graph->node + id_blur2v;
  *node_blur2v = (dt_node_t) {
    .name   = dt_token("blur2v"),
    .kernel = dt_token("blur2v"),
    .module = module,
    .wd     = wd,
    .ht     = ht,
    .dp     = dp,
    .num_connectors = 2,
    .connector = {
      ci, co,
    },
    .push_constant_size = 4,
    .push_constant = {2},
  };

  // TODO: context buffer handling:
  // TODO: if we have a context buffer, blur it in a separate set of nodes
  //       with a reduced blur radius

  // wire module i/o connectors to nodes:
  dt_connector_copy(graph, module, id_blur1,  0, 0);
  dt_connector_copy(graph, module, id_blur2v, 1, 1);

  // interconnect nodes:
  dt_node_connect(graph, id_blur1,  1, id_blur2h, 0);
  dt_node_connect(graph, id_blur2h, 1, id_blur2v, 0);

  // TODO: currently we require to init node->{wd,ht,dp} here too!
  // TODO: this would mean we'd need to re-create nodes for new roi, which indeed may be necessary for wavelet scales etc anyways
}
